# Clipboard: echo of the server's text, and the viewer chain

Planned fix, to be done after the `cpp-clean-up` branch is in use. Not
implemented yet.

## How it works now

`DesktopWindow` keeps the local clipboard and the server's cut text in
step, through `WinClipboard` (`win32-lib/win-system/WinClipboard.cpp`):

- Server to local: `DesktopWindow::setClipboardData()` puts the text on
  the clipboard with `WinClipboard::setString()`, then records it in
  `m_strClipboard`.
- Local to server: on `WM_DRAWCLIPBOARD`, `onDrawClipboard()` reads the
  clipboard and sends it to the server, unless it equals
  `m_strClipboard`, in which case it takes the change for its own and
  clears `m_strClipboard`.

## Problems

### Our own change can be sent back to the server

The text comparison is meant to recognise the notification of our own
`setString()`, but:

- I believe the system sends `WM_DRAWCLIPBOARD` to the first window of
  the viewer chain synchronously, while the clipboard is being changed
  (not measured). Then `onDrawClipboard()` runs before
  `setClipboardData()` has set `m_strClipboard`, and the server's text
  goes straight back to it.
- It recognises the text, not the change: while `m_strClipboard` still
  holds the server's text (as it does after the echo above), a local
  copy of the same text is taken for ours and dropped.

Timing is not the real issue, though. Windows notifies of every change,
ours included; what is needed is a reliable way to tell ours from the
others.

### The clipboard viewer chain is mishandled

`DesktopWindow` uses the legacy viewer chain (`SetClipboardViewer()`,
`WM_DRAWCLIPBOARD`, `WM_CHANGECBCHAIN`), in which each viewer must pass
every message on to the next one:

- It registers twice. `WM_CREATE` calls
  `SetClipboardViewer((HWND)wParam)`, but `WM_CREATE`'s `wParam` is
  unused, so that registers no window; its result is kept as
  `m_hwndNextViewer`. `WinClipboard::setHWnd()` then calls
  `SetClipboardViewer(m_hWnd)` and drops the next viewer it returns.
- Nothing calls `ChangeClipboardChain()` when the window is destroyed,
  so closing a viewer window can break the chain for the programs after
  it.

## Fix

1. Replace the viewer chain with `AddClipboardFormatListener()`
   (Windows Vista and later): no chain to pass messages along or
   repair, and `WM_CLIPBOARDUPDATE` is posted, not sent. Register in
   `WM_CREATE`, `RemoveClipboardFormatListener()` in `WM_DESTROY`, and
   remove `SetClipboardViewer()` from `WinClipboard::setHWnd()`, the
   `WM_CHANGECBCHAIN` and `WM_DRAWCLIPBOARD` handling and
   `m_hwndNextViewer`.
2. Recognise our own change by the clipboard sequence number.
   `GetClipboardSequenceNumber()` is incremented on every change to the
   clipboard, emptying included. `WinClipboard::setString()` reads it
   after `SetClipboardData()`, while it still has the clipboard open, so
   that no other program's change can come in between; a new
   `WinClipboard` function says whether the clipboard is still at that
   number. `onDrawClipboard()` (handling `WM_CLIPBOARDUPDATE`) sends
   nothing then. `m_strClipboard` and the text comparison go.

Considered and not chosen:

- `GetClipboardOwner()`: since `ef77169`, `setString()` always calls
  `EmptyClipboard()`, so after our change the owner is our window.
  Simpler, with no number to keep, but a program that adds a format
  without emptying the clipboard (some clipboard managers do) leaves us
  the owner, and its change would be ignored.
- A private clipboard format put beside the text as a marker: works,
  but puts extra data on the clipboard, seen by other programs and the
  clipboard history.
