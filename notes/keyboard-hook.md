# Keyboard hook and the special-key buttons

Notes from looking at how the viewer passes system keys to the server,
for when the toolbar's key buttons are revisited.

## When the hook is active

`ViewerWindow` installs a low-level keyboard hook (`WH_KEYBOARD_LL`,
through `WinHooks`) only in full-screen mode: when it enters full screen,
and when a full-screen viewer is activated. It removes the hook when it
leaves full screen or is deactivated.

- **Full screen, hook active**: the hook sees keys before Windows acts on
  them, and forwards them to the desktop window as key messages: Win,
  Alt+Tab, Ctrl+Esc, Alt+Esc and the like reach the server. Ctrl, Alt,
  Shift and the lock keys are let through to the normal key messages
  instead (`ViewerWindow::onHookProc()`).
- **Windowed, no hook**: Windows acts on Alt+Tab, Win and Ctrl+Esc
  locally, and the viewer drops Win key events on purpose
  (`DesktopWindow::setWinKeyIgnore(true)`, checked in
  `RfbKeySym::processKeyEvent()`).

## The buttons

- **"Send Ctrl-Esc"** (`commandCtrlEsc()`) sends Left Ctrl + Esc. Its icon,
  `toolbar-key-win.ico`, shows the Windows key, as TightVNC's did,
  because on a Windows server Ctrl+Esc opens the Start menu. On other
  desktops it means something else: on KDE Plasma it opens System
  Activity. In windowed mode it is the only way to open the remote Start
  menu; in full screen the hook makes it a convenience.
- **"Ctrl key down" / "Alt key down"** (`commandCtrl()`, `commandAlt()`)
  hold the key down on the server until clicked again. Mostly nice to
  have: they help with combinations that Windows takes for itself in
  windowed mode (Alt+Tab, Alt+Esc, Ctrl+Alt+Tab), with combinations that
  the local machine reserves (such as graphics driver hotkeys), and with
  mouse-only use. With the hook active they are rarely needed.
- **"Send Ctrl+Alt+Del"** is needed: Ctrl+Alt+Del is the Secure
  Attention Sequence, which Winlogon handles below any application hook,
  so no program can intercept it. The viewer also takes
  Ctrl+Alt+Shift+Del as a substitute. Win+L (lock) cannot be stopped by
  a low-level hook either.

## Possible changes

- Make the Windows-key button send the Windows key itself (`XK_Super_L`
  down and up), which matches its icon: Start on a Windows server, the
  launcher on Plasma. Keep Ctrl+Esc only if something needs it.
- Or make it a sticky Win button like Ctrl and Alt, so that Win+R and
  other Win combinations work in windowed mode.
- Reconsider whether the sticky Ctrl and Alt buttons earn their toolbar
  space.
