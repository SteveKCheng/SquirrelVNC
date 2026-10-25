# Clipboard: echo of the server's text, and the viewer chain

Fixed on the `clipboard-echo` branch (`67478af`, `36ea946`); see
[What was done](#what-was-done). The sections up to and including
[Fix](#fix) are the plan as written before, kept for the reasoning.

## How it worked before the fix

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

## What was done

1. `67478af`: `DesktopWindow::onCreate()` calls
   `AddClipboardFormatListener()`, and `onDestroy()`
   `RemoveClipboardFormatListener()`. `onDrawClipboard()` is now
   `onClipboardUpdate()`, handling `WM_CLIPBOARDUPDATE`.
   `SetClipboardViewer()`, the `WM_CHANGECBCHAIN` and `WM_DRAWCLIPBOARD`
   handling and `m_hwndNextViewer` are gone.
2. `36ea946`: `WinClipboard::setString()` records the sequence number
   of its change, and `WinClipboard::isUnchangedSinceSetString()` says
   whether the clipboard is still at it. `onClipboardUpdate()` sends
   nothing then. `m_strClipboard` is gone.

One change from the plan: the number is read just after
`CloseClipboard()`, not while the clipboard is still open, because
closing changes it again. A throwaway test program on the sandbox
(Windows 10 21H2, a child window registered as a listener) showed:

| Step                                         | Sequence number |
|----------------------------------------------|-----------------|
| Before `OpenClipboard()`                     | 0               |
| After `EmptyClipboard()`                     | 1               |
| After `SetClipboardData(CF_UNICODETEXT)`     | 2               |
| After `CloseClipboard()`                     | 5               |
| At our `WM_CLIPBOARDUPDATE`                  | 5               |
| Reading, including synthesised `CF_TEXT`     | 5 (unchanged)   |
| Another window adding a format, no emptying  | 6               |

The extra three are presumably `CF_LOCALE` and the other text formats
(`CF_TEXT`, `CF_OEMTEXT`), which were listed after closing; how many
there are is the system's business, so the code does not count on it.

The same run confirmed the rest of the plan:

- `WM_CLIPBOARDUPDATE` is posted: none had arrived when `setString()`
  returned, and one came when the message queue was next pumped.
- One notification per change, that is, per open/close of the
  clipboard, not per format.
- Adding a format without emptying leaves the previous owner in place,
  as assumed when `GetClipboardOwner()` was rejected.

The remaining gap: another program's change between `CloseClipboard()`
and `GetClipboardSequenceNumber()`, one call apart, would be taken for
ours and not sent to the server. Nothing is echoed in that case; one
local copy is missed.

`GetClipboardSequenceNumber()` returns 0 when the window station gives
no clipboard access. Then nothing is recorded, and every change is sent,
as if none were ours.

Not yet tested against a server; to check by hand:

- Copy on the server: the text arrives locally and is not sent back
  (in the viewer's log at detail level, no "Sending ... cut text"
  after it arrives).
- Copy on the server, then copy the same text locally: it is sent.
- Close one viewer window while another program (or a second viewer)
  watches the clipboard: it still gets changes.
