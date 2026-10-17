# Rebranding to SquirrelVNC, and icons from SVG

Notes on the second round of work on this fork (October 2026): removing
the commercial TightVNC material, renaming the program to SquirrelVNC,
and generating the icons from the SVG logo at build time. Commit hashes
refer to this repository. See `initial-changes.md` for the first round
and `old-server.md` for the server that was removed before this.

## Name

The program is **SquirrelVNC**: a play on TigerVNC, with a peaceful
herbivore as the mascot instead of a predator. It is a viewer only, so
the name has no "Viewer" suffix. A server, if there ever is one, would
be a separate repository with its own branding.

The repository is to be published at
<https://github.com/SteveKCheng/SquirrelVNC/>.

## Commits

The branches are linear, in this order, on top of `main` (`fbc6113`,
which added the logo in `art/`).

### `remove-commercial`

| Commit | Change |
|---|---|
| `e1442eb` | The last Windows-1251 text made ASCII: the Russian Visual Studio comment in `tvnviewer/resource.h`, and "Сlass" spelled with a Cyrillic С in `log-writer/ProfileLogger.h`. All sources are now ASCII apart from UTF-8 byte-order marks. |
| `59fd51e` | Removed the About dialog's "Commercial Licensing" button and paragraph, the connection dialog's "Licensing" button, their URLs and handlers, and `LoginDialog::openUrl()`. The connection dialog now says the source "is available to everyone under the GNU General Public License". |

### `license-text`

| Commit | Change |
|---|---|
| `ffd8046` | In 552 file headers, the TightVNC line becomes "This file is part of SquirrelVNC, a modified version of the TightVNC software (https://www.tightvnc.com/)". The GlavSoft copyright lines stay, as the GPL requires; third-party headers are unchanged. The About dialog says "Derived from TightVNC. Copyright (C) 2008-2026 GlavSoft LLC.", "version 2 or later", and has a short free-software and no-warranty notice. `LegalCopyright` in the version resource matches. |

### `rebrand`

| Commit | Change |
|---|---|
| `de09b2c` | The SRP-6 authentication error says what is wrong ("The server offers only SRP-6 authentication, which this viewer does not support…") instead of "requires TightVNC Viewer version 4". |
| `d0f757d` | Version resource: SquirrelVNC `3.0.0-pre` (numeric 3,0,0,0), empty CompanyName. `TargetName` makes the executable **`SquirrelVNC.exe`**. |
| `e4a8109` | The file transfer close confirmation is titled "VNC File Transfers". |
| `6c49e17` | `NamingDefs.cpp`: product name and window title "SquirrelVNC"; the main window title is "desktop - SquirrelVNC". |
| `09f6102` | Settings in `HKCU\Software\SquirrelVNC` (was `Software\TightVNC\Viewer`), log in `%APPDATA%\SquirrelVNC\SquirrelVNC.log`. TightVNC settings are not imported. |
| `ffa445f` | "Visit the Web Site" opens the GitHub repository. |
| `c3c7202` | `tvnviewer.rc`: dialog captions, the connection dialog's group box and text ("SquirrelVNC is a VNC viewer for Windows."), string table entries, and the command-line help (any VNC server; `SquirrelVNC` in the syntax). No control moved. Checked on screen. |

### `svg-icons`

| Commit | Change |
|---|---|
| `e731fd4` | `tools/svg2ico`, a build-time tool that renders an SVG into square icons (see below). |
| `7368a1e` | The `RenderIcons` target in `tvnviewer.vcxproj` renders `art/logo-publish.svg` into the application icon. `res/appicon.ico` removed. |
| `bbea3e1` | `gui/ScaledIcons`: window, dialog and tray icons are loaded at the sizes for the current DPI. |
| `7d75607` | The About and connection dialog logos are drawn from the application icon. `res/tightvnc.bmp` and `res/tightvnc48.bmp` removed. |

## What was kept

- **Protocol names.** `VendorDefs::TIGHTVNC` ("TGHT"), the Tight
  encoding and `TightDecoder`, and comments describing TightVNC
  protocol extensions. They name the protocol, not the product.
- **The `.vnc` file extension.**
- **Internal names.** The `TvnViewer` class, `TVN_*` constants, the
  window classes `TvnApplicationClass` and `TvnWindowClass`, the
  `tvnviewer/` directory and project, and `tightvnc.slnx`. They are
  invisible to users; they could be renamed in one mechanical commit
  later.
- **`tvnviewer/cmdline-help.docx`**, a Word copy of the command-line
  help, to be reviewed for anything worth keeping. Documentation is to
  be in Markdown; there will be no WinHelp/HTML Help.

## Icons

### Renderer

`svg2ico` renders with **Direct2D's SVG support** (Windows 10 1703 and
later), chosen over lunasvg from vcpkg. Other vcpkg options looked at:
plutosvg, thorvg, nanosvg (weak anti-aliasing), librsvg 2.40 (old C
version with a large GTK dependency tree), and qtsvg, skia and
GraphicsMagick (too heavy). resvg is not in vcpkg.

- For: no dependency, good anti-aliasing, and the same renderer the
  program could use at run time (for example for per-monitor DPI).
- Against: output can differ slightly between Windows versions, so it
  is not byte-for-byte reproducible across build machines; supports
  static SVG 1.1 only (no text, filters, masks, patterns, markers), and
  silently skips what it does not support.
- If Direct2D proves unsatisfactory, lunasvg is the fallback.

It renders into a WIC bitmap render target, which is always software
rendered. It works without a GPU or an interactive session: it was
tested over SSH in session 0. The Win32 and x64 builds of the tool give
byte-identical PNGs.

### The art

- `art/logo-inkscape.svg` is the Inkscape master. It uses Inkscape path
  effects, so it is not what the build reads.
- `art/logo-publish.svg` is exported from it with the path effects
  applied, as "optimized SVG" (choose SVG 1.1). It is only paths in
  groups with plain fills, which Direct2D renders fully.
- The logo's viewBox is 1567.7 × 1129.2, wider than tall (1.39:1).
  The icons centre it with no margin, leaving empty space above and
  below. This was chosen from a proof sheet of four variants; the logo
  stays legible down to 16 px. The art may later be squeezed
  horizontally by about 5%, but not more without spoiling the
  composition.

### `svg2ico`

```
svg2ico [--ico FILE] [--png-dir DIR] [--png-prefix TEXT] [--sizes LIST]
        [--margin PERCENT] [--align center|start|end] [--no-snap] input.svg
```

- Scales the viewBox to fit the square icon, keeping the aspect ratio.
  `--margin` leaves a percentage of the size empty on each side;
  `--align` places the drawing along its shorter axis. The offset is
  rounded to whole pixels unless `--no-snap` is given.
- Default sizes 16, 20, 24, 32, 40, 48, 64, 96, 256: the small and
  large icons at 100–200% scaling, plus Explorer's large views.
- The .ico stores every image as 32-bit PNG, which Windows Vista and
  later accept at any size.
- The project uses only Windows libraries and sets
  `VcpkgEnabled=false`.

### Build integration

- `tvnviewer.vcxproj` has a project reference to `svg2ico`, so the tool
  is built first.
- The `RenderIcons` target runs before `ResourceCompile`, writing
  `$(IntDir)icons\SquirrelVNC.ico`. That directory is on the resource
  compiler's include path, so `tvnviewer.rc` names just
  `"SquirrelVNC.ico"`. The target is skipped when neither the SVG nor
  `svg2ico.exe` changed.
- No generated binaries are in the repository.

### Loading at run time

`gui/ScaledIcons` loads an icon at a given pixel size with
`LoadIconWithScaleDown()`: the exact image if the .ico has it,
otherwise the next larger one scaled down by Windows.

- Icons from it are not shared like `LoadIcon()`'s, so it caches them
  for the life of the process and nothing destroys them. That keeps
  class icons valid after the window that set them closes (there can be
  several viewer windows). The process is system DPI aware, so the
  sizes do not change while it runs.
- `BaseWindow::loadIcon()` and `BaseDialog::loadIcon()`/`updateIcon()`
  set both the large (`SM_CXICON`) and small (`SM_CXSMICON`) class
  icons. Before, only the large icon was set and Windows shrank it.
- `Icon(DWORD)`, used only for the listening-mode tray icon, loads the
  small icon.
- The dialog logos are owner-drawn statics (`IDC_LOGO`) in the old
  boxes (About: 60×55 dialog units; connection dialog: 32×30).
  `ScaledIcons::drawCentered()` draws the icon as the largest square
  that fits, centred.

## Follow-ups

- **Dialog logos are smaller than they could be.** The boxes are about
  square (roughly 90×89 px and 48×48 px at 100%), and the logo inside
  the square icon is only 72% as tall as it is wide, so it is drawn
  about 90×64 and 48×35 px, while the dialogs have unused space beside
  the boxes. Possible fixes: widen the boxes and draw the logo at its
  own aspect ratio (a non-square image rendered by `svg2ico`, or
  Direct2D at run time), so its width can grow into the free space.
- **Sizes between .ico images** (such as the About logo, about 89 px at
  100%) are scaled by Windows from the next larger image rather than
  rendered by Direct2D. Adding intermediate sizes to the .ico, or
  rendering at run time, would avoid that.
- **Toolbar:** its 16 buttons are still the 16×16 4-bit bitmap
  `res/toolbar.bmp`. They need SVG art of their own; then `svg2ico`
  can render them and `gui/ToolBar.cpp` can use an image list at the
  DPI size (`ImageList_Create` with `ILC_COLOR32`, `TB_SETIMAGELIST`)
  instead of `TB_ADDBITMAP`.
- **The remaining icons** (`ftfile.ico`, `fileup.ico`, `ftdir.ico` for
  file transfer, and the cursors) are still the old TightVNC files.
- **Version from Git:** `3.0.0-pre` is hard-coded. The plan is to
  derive it with GitVersion.MsBuild and custom vcxproj targets.
- **Settings in a file:** move from the registry to
  `%APPDATA%\SquirrelVNC\default.ini`, with a portable mode that can
  redirect the settings and log paths.
- **Per-monitor DPI v2,** which would need icons re-loaded at the new
  DPI (the `ScaledIcons` cache assumes a fixed DPI).
- **Before publishing:** add a `COPYING` file with the GPLv2 text and a
  README with attribution to TightVNC.
