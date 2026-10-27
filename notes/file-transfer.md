# File transfer: known issues left for later

The TightVNC file transfer (`core-lib/ft-client-lib`, `ft-common`, and
`tvnviewer/FileTransferMainDialog`) is kept working, but is not a
priority: SMB or SFTP are better protocols, with better
implementations. These issues were found during the `cpp-clean-up`
branch and deliberately left alone.

## Upload follows symbolic links and junctions

`UploadOperation` walks a local folder with `File::list()` and
`File::isDirectory()`, in `getFileSize()` (the total size, recursively)
and `processFolder()` (through `FolderListener`). A symbolic link or
junction to a directory is a directory to both, so:

- The upload sends what the link points to, as if it were a folder in
  the tree, possibly from outside the folder the user chose.
- A link to a folder above it makes a cycle: `getFileSize()` recurses
  until the stack overflows, and the upload itself never ends.

Deleting does not have the problem since `3d39fca`:
`LocalFilesDeleteOperation` removes a link with `File::remove()` and
does not go into it, using `File::isLink()`.

What upload should do is less obvious than for delete. Options:

- Skip links, perhaps with a message (the least surprising for a
  cycle, but drops data the user may expect).
- Send each link's target once, keeping track of the directories
  already visited (by file ID, from `GetFileInformationByHandle()`),
  which also breaks cycles.
- Send the link as a link: the protocol has no way to.

## ~~Operation threads are not waited for~~

*Fixed by Phase 4 step 5d of `async-networking.md` (2026-10-27): the
delete runs on the shared `BlockingWorkPool`, holding a claim on its
viewer instance until it ends, so the operation and what it reports to
are not freed under it; closing the viewer window asks it to stop. The
text below describes the code before.*


`LocalFilesDeleteOperation`, the one operation that derives from
`Thread`, does its work on a thread of its own, but nothing waits for
that thread before the operation is destroyed:

- `Thread::~Thread()` only closes the handle (its documentation says so:
  "does not stops thread execution if it's still running").
- `FileTransferCore::executeOperation()` frees the previous operation
  when it starts the next one, and `~FileTransferCore()` frees the
  current one.

If either happens while the thread is still running (a long delete,
then another operation, or the dialog closed), the thread goes on using
the freed operation. Whether the dialog prevents that by disabling its
controls during an operation has not been checked.

Waiting for the thread in the destructor is not a simple fix: the
thread reports progress and errors through `FileTransferCore` to the
dialog, which sends window messages, so the UI thread waiting for it
could deadlock. To be solved when the core moves to portable C++
threads (`std::thread` or `std::jthread`), with a cancellation and
reporting design that does not block the UI thread; see
`concurrency.md`.

## Cursor is null after an operation ends

`DownloadOperation::killOp()`, `UploadOperation::killOp()` and
`RemoteFilesDeleteOperation::killOp()` free the tree of files and set
the cursor (`m_toCopy`, `m_toDelete`) to null. A reply that the server
sends after that, unasked, can reach handlers that use the cursor
without checking it (for example `gotoNext()`, which starts with
`m_toCopy->getChild()`), and dereference the null pointer: a crash,
which a server can cause.
