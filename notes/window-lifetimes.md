# Window and dialog lifetimes

How the C++ window classes (`BaseWindow`, `BaseDialog` in `win32-lib/gui`)
and their Win32 windows end, what went wrong, and the design to move to.
Written 2026-10-27, from a discussion with Steve after a crash in the
file-transfer dialog (`security.md`, "Window objects freed before their
windows").

## The problem

A window and its C++ object can end in either order:

- **The window first.** The user closes it, `DestroyWindow()` is called
  from a handler, the system destroys it with its owner, or a modal
  dialog's `EndDialog()`. The object lives on, holding the handle.
- **The object first.** Whatever owns the object deletes it while the
  window is still open: for example, the viewer instance, which owns the
  file-transfer dialog, is deleted after the viewer window closes.

What went wrong, or could:

1. **An object freed before its window.** `BaseDialog` did not destroy
   its window, so the window kept a `GWLP_USERDATA` pointing at freed
   memory, and its next message called into it. The file-transfer dialog
   has no owner window, so it survived its viewer window, and was freed
   with the viewer instance while still open: a use-after-free a server
   could cause by disconnecting.
2. **Cleanup from a base-class destructor.** `~BaseWindow` calls
   `DestroyWindow()`, which sends `WM_DESTROY` and `WM_NCDESTROY`
   synchronously into the window procedure, whose virtual handlers then
   run as the base class's: the subclass is already destroyed. Its
   `onDestroy()` cleanup never runs. (`ViewerWindow` avoids this with its
   own `destroy()`, called while it is whole.)
3. **A stale handle.** An object whose window ended first still holds its
   handle; destroying the object later destroys whatever window has
   reused it. Open for `BaseWindow` (`~BaseWindow` calls `DestroyWindow()`
   on a handle that may be stale).
4. **The application's modeless-dialog list** keeps every handle added
   (`WindowsApplication::addModelessDialog()`), and `IsDialogMessage()` is
   called on each, so a reused handle could make it act on an unrelated
   window. `TvnViewer`'s dialogs add their handle again each time they
   are shown.

## Fixed now

In the `[SECURITY]` commit that fixed the crash:

- `BaseDialog::dialogProc()` detaches on `WM_NCDESTROY`, the last message:
  it clears `GWLP_USERDATA` and the object's handle, so neither refers to
  the other once the window is gone.
- `~BaseDialog` detaches its window, if it still has one, then destroys it
  if it is modeless. Detaching first means the destruction's messages
  find no object, rather than a half-destroyed one, and no handler runs.
  A subclass whose `onDestroy()` must run destroys its window in its own
  destructor, while it is whole, as `~FileTransferMainDialog` does.
- The file-transfer dialog removes itself from the modeless-dialog list
  when destroyed (`WindowsApplication::removeModelessDialog()`), and
  `processDialogMessage()` iterates over a copy of the list, since a
  dialog may remove itself during `IsDialogMessage()`.

`BaseWindow` is unchanged.

## The design to move to

The usual C++ answer, and the established Win32 one (MFC's
`PostNcDestroy()`, ATL's `OnFinalMessage()`): the object's lifetime
follows the window's, and cleanup lives in destructors, like any other
resource-managing class.

- **`WM_NCDESTROY` detaches, then calls a release hook.** The window
  procedure clears `GWLP_USERDATA` and the handle, then calls the hook,
  which ends the object's claim on itself.
- **The hook is a callable given by the creator, not a virtual function.**
  The creator decides allocation and ownership: the class cannot know
  whether it is on the stack, a member, in a `std::unique_ptr` or shared,
  just as `std::unique_ptr`'s deleter is part of the allocation, not of the
  type. ATL's virtual `OnFinalMessage()` is the counter-example: a class
  written to delete itself cannot then be put on the stack. The hook is a
  `std::function` given at creation (`create()`, `createWindow()`), so no
  window exists without its policy; an empty one does nothing, for
  dialogs on the stack and members. A `std::function` rather than a
  function pointer, since the useful hooks capture state; it runs once per
  window, so its cost does not matter.
- **The hook usually defers.** A window is often destroyed from its own
  handlers (`kill()` inside `onMessageReceived()`, with `dialogProc()`
  still using the object afterwards), sometimes under a nested modal loop,
  so `delete this` at `WM_NCDESTROY` frees the object under live frames.
  The safe hooks are "nothing", and "post the release to the top of the
  message loop" (`WindowsApplication::postToMessageLoop()`, as
  `ViewerCollector`'s deleter does); synchronous `delete this` only where
  provably safe.
- **The other order still works.** A destructor that finds its window
  still there detaches it, then destroys it, as `~BaseDialog` now does;
  the messages find no object, and the hook is not called again.
- **Cleanup moves into destructors.** Either way, the derived destructors
  run once, while the object is whole; `onDestroy()` remains only for
  reacting to the window going (UI), not for releasing resources.
- **The file-transfer dialog would own itself**: its hook releases it, and
  `ViewerWindow` keeps a non-owning pointer, cleared by the release;
  destroying the viewer window destroys the dialog's window, and so the
  dialog. Today `ViewerWindow` owns it in a `std::unique_ptr` that
  outlives the window, which is why cleanup tied to the window (detaching
  the events) and cleanup tied to the object are separate.

To do as a piece of work of its own, after Phase 4 of
`async-networking.md`: it touches every window class. The `BaseWindow`
stale handle (3) and the modeless-dialog list (4, generally) go with it.
