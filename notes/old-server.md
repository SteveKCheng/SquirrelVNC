# The TightVNC server (before removal)

TightVNC 2.8.90 includes a Windows VNC server (`tvnserver.exe`) as well
as the viewer. This fork is dropping the server to concentrate on the
viewer. These notes record how the server worked and why it was
dropped, for anyone who wants to bring parts of it back. The code
remains in the git history before the commit that removes it.

## Why it was dropped

- The fork's aim is a modern Windows **viewer**, for example for using a
  laptop as an extra monitor for a Linux desktop exported by wayvnc. The
  maintainer has no use for the server, so changes elsewhere would leave
  it untested.
- Windows ships RDP, which beats a VNC server on Windows in practice.
  RDP runs in its own session rather than sharing the physical console,
  gets drawing commands from the graphics stack rather than finished
  pixels, and encodes with hardware H.264/AVC444. The TightVNC server
  shares the console and encodes pixels on the CPU.
- VNC's strength is a simple protocol for non-Windows desktops and
  specialized uses: kiosks, industrial displays, single-window apps, 
  extra monitors. Those are client-side concerns for this project.
- GUI test automation, a traditional use for VNC, is better done with the
  platform's accessibility APIs.
- TightVNC's server likely has security vulnerabilities like what has been
  found in the rest of the code, and this project does not have bandwidth to 
  address all those.
- The remaining reasons to run a VNC server on Windows are Windows Home
  editions (no RDP host) and remote support for the person at the
  console.

## Architecture

### Processes

`tvnserver.exe` is one executable with several modes, selected on the
command line:

- **Service** (`-service`): runs in session 0, which cannot see the
  user's desktop. It starts a helper copy of itself in the console
  session and follows session changes.
- **Desktop server** (`-desktopserver`, helper): runs in the console
  session, captures the screen and injects input (`SendInput`, plus
  simulated Ctrl+Alt+Del via SAS). It talks to the service over pipes and
  shared memory (`desktop-ipc`).
- **Application** (`-run`): a single process in the user's session that
  does everything itself.
- **Control** (`-controlapp`/`-controlservice` for the tray icon,
  `-configapp`/`-configservice` for the configuration UI;
  `tvncontrol-app`, `wsconfig-lib`). It talks to the server over a named
  pipe ("control protocol"). The same interface backs command-line
  actions such as `-connect` (reverse connection), `-disconnectall`,
  `-shutdown`, `-reload` and `-sharewindow`/`-sharerect`/`-sharedisplay`
  (share only part of the screen).
- **Service management** (`-install`, `-remove`, `-start`, `-stop`) and
  `-queryconnection` (the dialog asking the user to accept an incoming
  connection, started as a separate process in the user's session).

Helpers: `hookldr.exe` loads the 32-bit `screenhooks` DLL on 64-bit
Windows. `setup-helper.dll` holds installer custom actions. Log messages
from the helper processes are collected by `log-server`.

### Screen capture (`desktop/`)

`Win32ScreenDriverFactory::createScreenDriver()` tries these in order:

1. **Desktop Duplication API** (`Win8ScreenDriver`, `Win8DeskDuplicationThread`,
   `WinDxgi*`, `WinD3D11*`; Windows 8 and later; allowed by default,
   setting `D3DAllowed`). `IDXGIOutputDuplication` gives each new frame
   as a GPU texture plus the dirty rectangles
   (`GetFrameDirtyRects`), moved areas (`GetFrameMoveRects`, sent to
   clients as CopyRect) and the pointer shape
   (`GetFramePointerShape`). The changed parts are copied into a staging
   texture (`CopySubresourceRegion`) and mapped into system memory
   for encoding. One duplication thread runs per output.
2. **Mirror driver** (`Win32MirrorScreenDriver`, `MirrorDriverClient`):
   the DFMirage kernel-mode driver from the Windows XP era, installed
   separately. Mirror drivers are not supported since Windows 8.
3. **GDI** (`Win32ScreenDriver`, `WindowsScreenGrabber`): `BitBlt` of
   the screen. Changes are found with global window hooks
   (`screen-hooks`, `HooksUpdateDetector`), polling (`Poller`,
   `ConsolePoller`) and copy-rect guessing (`CopyRectDetector`).

The cursor shape and position, clipboard and wallpaper hiding
(`WallpaperUtil`) are handled separately.

### Serving clients

- `network`: socket listener for RFB (port 5900) and HTTP (5800).
- `rfb-sconn`: one connection per client. Protocol handshake, VNC
  password authentication (DES), the Tight security type with capability
  lists (vendor `TGHT`), client messages, and the encoders: Tight (zlib
  plus JPEG via libjpeg), ZRLE, Hextile, RRE, Raw, plus
  pseudo-encodings for cursor shape, cursor position, desktop size and
  compression/quality levels.
- `fb-update-sender`: keeps the changes for each client and sends an
  update when that client requests one. It is the same
  request-and-reply loop as the viewer, with no ContinuousUpdates or
  Fence. All encoding is on the CPU.
- `ft-server-lib`: the server side of TightVNC's file transfer extension.
- Extras: reverse connections, per-IP access rules, a query dialog
  for accepting incoming connections, Windows event log messages
  (`win-event-log`), and the HTTP server (`http-server-lib`). The HTTP
  server serves the Java applet viewer, which is embedded as a C array
  (`VncViewerJarBody.h`, most of the library's 23k lines) and has been
  useless since browsers dropped Java.

### Server-only directories

| Directory | Lines (.cpp/.h) | Purpose |
|---|---|---|
| `tvnserver` | 0.5k | Executable entry point and resources |
| `tvnserver-app` | 6.7k | Modes, service, session tracking, server core |
| `desktop` | 13.3k | Screen capture, input, clipboard, cursor |
| `desktop-ipc` | 2.9k | Service ↔ desktop server protocol |
| `rfb-sconn` | 5.7k | Per-client RFB connection and encoders |
| `fb-update-sender` | 2.2k | Per-client update queues |
| `ft-server-lib` | 1.3k | File transfer, server side |
| `server-config-lib` | 3.5k | Server settings |
| `wsconfig-lib` | 4.2k | Server configuration dialogs |
| `tvncontrol-app` | 6.3k | Tray icon and control client |
| `http-server-lib` | 23.0k | HTTP server and embedded Java viewer |
| `log-server` | 1.6k | Log collection from helper processes |
| `screen-hooks` | 0.3k | Hook DLL for the GDI driver |
| `hookldr` | 0.4k | Loads the 32-bit hooks on 64-bit Windows |
| `win-event-log` | 0.2k | Windows event log messages |
| `setup-helper` | 0.3k | Installer custom actions |
| `wix-installer` | – | WiX installer sources |

In total about 72k lines, against about 63k for the viewer and the
libraries it shares with the server. Nothing in the viewer or the shared
libraries includes a header from these directories. Some server-only
code remains in the shared libraries, such as services, impersonation,
WTS and Ctrl+Alt+Del simulation in `win-system`.

## Windows as an extra monitor (idea, not pursued)

The Desktop Duplication code could be the basis of a "laptop as extra
monitor" server for Windows, like wayvnc exporting a virtual output on
Linux. That needs a virtual monitor:

- Windows has no user-mode API to create a display output. It needs a
  driver, written for the **Indirect Display Driver** model (IddCx,
  Windows 10 1607 and later). That's a user-mode (UMDF) driver in the
  public WDK, with a Microsoft sample (`IddSampleDriver`). The driver
  reports a monitor (EDID and modes) and receives each frame through
  a swap chain, so it could also hand the frames straight to an encoder
  without Desktop Duplication.
- Existing open-source IddCx drivers include the
  [Virtual Display Driver](https://github.com/VirtualDrivers/Virtual-Display-Driver)
  and SudoVDA, which game streaming hosts such as Sunshine and Apollo
  use. Their monitors show up as ordinary outputs, and Desktop
  Duplication captures them.
- The catch is driver signing. Production drivers need a signed driver
  package. Hobby projects either use a certificate the user must trust
  or a signing service.
- For Windows-to-Windows use, Windows already has this feature built in:
  "Projecting to this PC" (Miracast sink, also over the LAN) lets
  another PC extend its desktop onto a laptop.
