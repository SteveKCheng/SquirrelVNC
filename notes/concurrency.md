# Concurrency: threads, locks and queues

**Historical once replaced.** This is the original design from
TightVNC. It is to be replaced by one event loop and C++20 coroutines
(`async-networking.md`), after which this note is kept, like
`old-server.md`, as a record of the starting point.

How the viewer handles decoding, rendering, network I/O and GUI events
concurrently, as inherited from TightVNC (investigated 2026-10-20). No
changes have been made from this yet; it is input for deciding what to
do and in what order.

Summary: **one thread per job, blocking sockets, mutexes on shared
data and a few events.** There are no task queues of its own; the
Windows message queue is used in some places.

## Threads

| Thread | Does | Created by |
|---|---|---|
| **GUI** (main) | Runs the one message loop for all windows (every viewer window, the login dialog, the tray icon). Paints; handles keyboard and mouse. **Also writes key, pointer and clipboard messages directly to the socket.** | `TvnViewer::processMessages` |
| **Core**, one per connection | Connects, handshakes and authenticates, then a blocking loop: read a message, decode rectangles, send the next update request. | `RemoteViewerCore` (a `Thread`) |
| **Notifier**, one per connection | Takes the accumulated changed region and calls the GUI's adapter callbacks, which copy pixels into the window's DIB. | `FbUpdateNotifier` |
| Listener (listening mode only) | Waits for incoming connections. | `TcpServer` → `ConnectionListener` |
| File-transfer delete | Deletes local files. *Since Phase 4 step 5d of `async-networking.md`, on the shared `BlockingWorkPool` thread instead, started on first use.* | `LocalFilesDeleteOperation` |

Those are the only `Thread` subclasses. Every window lives on the GUI
thread. Until 2026-10-25 there was one exception: the **password dialog
ran on the core thread**. `ViewerVncAuthHandler::getPassword()` is
called from `authenticate()` and showed a modal `AuthenticationDialog`
there, with its own message loop. ~~Now (step 2 of Phase 1 in
`async-networking.md`) it calls `ViewerWindow::askPassword()`, which
posts `WM_USER_PASSWORD` and waits on a `std::future`; the GUI thread
shows the dialog and fulfils the promise.~~ *From Phase 4 step 2d
(2026-10-26), the password is a request that the network thread serves
without waiting: `ViewerVncAuthHandler::requestPassword()` calls
`ViewerWindow::requestPassword()`, which posts `WM_USER_PASSWORD`; the
GUI thread shows the dialog, and its answer resumes the connection on
the network thread. Other connections go on meanwhile.* Two
consequences:

- ~~The GUI thread must never wait for a core thread that may be waiting
  for it. `ViewerInstance` therefore calls
  `ViewerWindow::cancelPasswordRequest()` before waiting; the
  cancellation also refuses requests made after it, so a core thread
  that asks just too late does not wait either.~~ *A request must
  always be answered, so that the connection can end:
  `ViewerWindow::cancelPasswordRequest()`, when the window closes or the
  connection stops, answers it with nothing, and refuses later ones.*
- The dialog's modal loop now runs on the GUI thread, so other windows
  keep working through it, but without `TvnViewer::processMessages`:
  no accelerators, and no `IsDialogMessage` for modeless dialogs, while
  it is open. ~~`ViewerWindow::isStopped()` returns false during the
  dialog, so that `ViewerCollector::deleteDeadInstances()`, which can
  run from a timer inside that loop, cannot delete the instance whose
  handler is still on the stack.~~ *Since Phase 4 step 2c was redone
  (2026-10-26), an instance is deleted only from the top of the
  application's message loop (`WindowsApplication::postToMessageLoop()`),
  never inside a nested loop, so no exception for the dialog is needed.*

## A framebuffer update, end to end

1. **Core thread.** The decoder reads from the blocking socket and
   decodes into a private second buffer (`m_rectangleFb`), with no lock.
   It then copies the rectangle into the shared `m_frameBuffer` while
   holding `m_fbLock` (`DecoderOfRectangle::copy`). Finally it calls
   `FbUpdateNotifier::onUpdate(rect)`, which adds the rectangle to a
   `Region` under `m_updateLock` and signals a `WindowsEvent`.
2. **Notifier thread.** Wakes, takes the whole region and clears it.
   While holding `m_fbLock`, it draws the remote cursor into the
   framebuffer and calls `ViewerWindow::onFrameBufferUpdate` once per
   rectangle.
3. **The same callback, still on the notifier thread.**
   `DesktopWindow::updateFramebuffer` copies the pixels into the window's
   DIB section **without** `m_bufferLock`. A comment there says a torn
   frame is accepted and the next update fixes it. It then calls
   `InvalidateRect`.
4. **GUI thread.** Windows turns the invalidated area into a `WM_PAINT`.
   `onPaint` takes `m_bufferLock` and draws the DIB to the screen with
   `BitBlt` or `StretchBlt`.

So each update is copied three times: decode buffer → core framebuffer
→ DIB → screen.

The part that works like a queue is the **merging of changes**: the
notifier's `Region` plus its event, and Windows' own update region
behind `InvalidateRect`. Both collapse a burst of changes into one, so
a slow painter never builds up a backlog.

## How work is synchronized

Mostly **mutexes on shared data plus flags**. There are about 22
`LocalMutex` instances; `RemoteViewerCore` alone has seven (`m_fbLock`,
`m_pixelFormatLock`, `m_refreshingLock`, `m_freezeLock`,
`m_requestUpdateLock` and others), each guarding one or two fields.

Other threads ask the core for work by setting a flag that it reads
before its next update request: `refreshFrameBuffer()` sets
`m_isRefreshing`, `setPixelFormat()` sets `m_isNewPixelFormat`. There
is no message to the core thread.

`WindowsEvent` (an auto-reset event) is used only to wake the notifier.

## The Windows message queue

`PostMessage` to a window owned by another thread is allowed and
thread-safe; it is the standard way for worker threads to hand results
to the GUI. (`PostThreadMessage` exists too, but its messages are lost
while a modal loop is running.) The code uses `PostMessage` for:

- **Core → GUI:** disconnect, error and authentication error. The core
  stores the error in a member, then posts `WM_USER_DISCONNECT`,
  `WM_USER_ERROR` or `WM_USER_AUTH_ERROR`.
- **Listener → GUI:** it pushes the accepted socket onto a
  mutex-guarded list, then posts `WM_USER_NEW_LISTENING`. That is a
  proper hand-off queue.
- **File transfer:** `WM_OPERATION_FINISHED`.
- **Keyboard hook:** re-posts captured keys to the desktop window.
- **Login dialog:** its "About" and "Configuration" buttons post to the
  application instead of calling it directly.

It was not used consistently. Several callbacks touched the GUI
directly from the core thread:

- `ViewerWindow::onConnected()` called `showWindow()`,
  `setForegroundWindow()` and `applySettings()`;
- `onBell()` called `ShowWindow`;
- `onCutText()` opened the clipboard.

Many window calls made from another thread are turned by Windows into a
`SendMessage`, which blocks the calling thread until the GUI thread
handles it. They work, but they hide synchronous waits between threads.
**Changed 2026-10-25** (step 1 of Phase 1 in `async-networking.md`):
these three now post `WM_USER_CONNECTED`, `WM_USER_BELL` and
`WM_USER_CUT_TEXT`, and the work on the window is done on the GUI
thread. This also removed data races on `ViewerWindow::m_isConnected`
and `m_sizeIsChanged`, and on `WinClipboard`'s record of the sequence
number after its own change, which `onClipboardUpdate()` reads on the
GUI thread. `onConnected()` still sets up file transfer on the core
thread, before posting. The framebuffer callbacks on the notifier
thread still call into `DesktopWindow` directly, but only copy pixels,
(re)create the DIB section and call `InvalidateRect`, none of which
waits for the GUI thread.

**Nothing goes from the GUI to the network through a queue.** Key and
pointer events are written to the socket on the GUI thread
(`RfbKeyEventClientMessage::send` locks the `RfbOutputGate`, writes and
flushes), and file-transfer requests do the same. If the TCP send buffer
fills, because of a slow link or a stalled server, **the GUI thread
blocks inside `send()`**.

## Sockets are blocking

`recv`, `send` and `connect` are plain blocking calls, with no `FIONBIO`
and no `SO_RCVTIMEO` or `SO_SNDTIMEO`. The only `select()` is in the
listener's `accept()` loop, with a timeout so it can notice
termination.

Cancellation works by another thread closing or shutting down the
socket, which makes the blocked `recv` fail (`RemoteViewerCore::stop()`
→ `TcpConnection::close()`). As `networking.md` says, lookup and
connect cannot be cancelled this way.

## Hazards

None of these is a security issue; all are latent.

1. ~~**Possible deadlock on shutdown.**
   `ViewerCollector::destroyAllInstances()` holds `m_lockObj` on the GUI
   thread and waits for each core thread (`waitViewer()`). A core thread
   that is inside `onConnected()` at that moment is blocked in a
   cross-thread `SendMessage` to the GUI thread, which is now waiting on
   it. The window is narrow: closing the application just as a
   connection completes.~~ **Not reachable** (checked 2026-10-25), and
   the cross-thread calls are gone anyway (see "The Windows message
   queue"):
   - `destroyAllInstances()` runs only from `~TvnViewer`, after the
     message loop has ended, and the loop ends (`shutdown()`, from
     `TvnViewer::onTimer`) only once there are no instances left.
   - The other wait on a core thread, `~ViewerInstance` from
     `deleteDeadInstances()`, happens only after the viewer window has
     been destroyed; window calls on a destroyed `HWND` fail at once
     instead of waiting.
   - Latent in the same code: `destroyAllInstances()` "stops" each
     instance by posting `WM_USER_STOP` to its window, which nothing
     will process once the loop has ended, and then waits for its core
     thread, which nothing has stopped. If it were reached with a live
     connection, it would wait until the server disconnected. *Fixed
     2026-10-26 (Phase 4 step 2c): `ViewerInstance::stop()` destroys the
     window itself, which stops the connection, and
     `destroyAllInstances()` runs the deletions while it waits.*
2. **Data races that Windows tolerates:**
   - pixels written into the DIB while it is being painted (deliberate,
     see above);
   - `DesktopWindow::m_scManager` read in `repaint()` on the notifier
     thread while the GUI thread changes it in `onSize` /
     `calcClientArea` without the lock;
   - `m_isBackgroundDirty`, ~~and the core's `m_error` /
     `m_disconnectMessage`,~~ written without locks (the later
     `PostMessage` gives ordering in practice); *the error and disconnect
     message, and `ViewerWindow::m_stopped` read on the network thread,
     fixed 2026-10-26: they belong to `ViewerCoreEvents` now, under its
     lock, and the network thread no longer reads `m_stopped`;*
   - `Thread::m_terminated` is a `volatile bool`. That works with MSVC's
     x86/x64 `volatile` semantics, but not under ISO semantics (Clang,
     or MSVC on ARM64). It should be `std::atomic<bool>`.
3. **Locks held while calling out.** `m_fbLock` is held across the GUI
   adapter callbacks, and `DesktopWindow::setNewFramebuffer` then takes
   `m_bufferLock` inside it (its own comment says `FIXME: Nested locks
   should not be used`). File-transfer operations notify their listeners
   while holding the listener-list lock.
4. **Display glitch seen when changing the local cursor** (Steve,
   2026-10-26, while testing Phase 1 step 6). Changing the local cursor
   option during a session blanked the screen once; afterwards only
   parts were redrawn, with black rectangles until the remote screen
   changed there. Similar glitches were seen with TightVNC before,
   worse on slow connections, so it is probably not new. Not
   investigated: to be audited once the threading described here is
   gone. Candidates: `applySettings()` resizing or rescaling the window
   (`doSize()`, `DesktopWindow::setScale()`) while the notifier thread
   copies into the DIB without `m_bufferLock` (see "A framebuffer
   update, end to end"), and `m_isBackgroundDirty` written without a
   lock.

## Which code needs a recursive mutex

`LocalMutex` is a `CRITICAL_SECTION`, which is recursive. Searching for
functions that take a lock and then call a function of the same class
that takes the same lock, then checking each hit by hand, confirmed:

| Lock | Nesting | Thread |
|---|---|---|
| `DesktopWindow::m_bufferLock` | `onPaint` → `doDraw` → `drawImage`: three acquisitions on every paint | GUI |
| `FileAccount::m_logMut` | `flush` → `format`; `init` → `setNewFile` → `openFile` | any thread that logs |
| ~~`ViewerCollector::m_lockObj`~~ *(removed 2026-10-26)* | ~~`destroyAllInstances` → `deleteDeadInstances`~~ | GUI |

Plausible but unconfirmed: `WindowsApplication::processDialogMessage`
holds `m_MDLMutex` while `IsDialogMessage` dispatches to dialogs. A
modal dialog opened from there (Options) runs its own loop, which can
dispatch the posted `WM_USER_CONFIGURATION` / `WM_USER_ABOUT`, and those
call `addModelessDialog` on the same thread. That lock only ever sees
the GUI thread, so it is not needed at all.

Two hits were false: `receiveFbUpdate` and `sendFbUpdateRequest`
release their locks before making the calls.

Each confirmed case has the usual fix: a public function takes the lock
and calls a private `…Locked()` function that assumes it is held. They
are small changes; `m_bufferLock` is the only one on a hot path.

The search covers only calls within one class, so nesting through
callbacks into other classes (such as the file-transfer listeners) is
not ruled out. Making recursion a fatal error in a debug build of
`LocalMutex` would catch the rest at run time, but that needs a desktop
to exercise.

## Direction

A task-based model would fit:

- GUI → network becomes a single-writer send queue drained by the
  network side, so the GUI never blocks on `send()`;
- core → GUI goes through `PostMessage` only;
- the notifier's merging of changed regions stays, as a mailbox.

That would also remove most of the locks, and with them the need for
recursion. It is a larger refactor than the steps in `portability.md`,
whose rewrite of `thread` on the standard library is where a
non-recursive mutex would be introduced.
