# Dead code removed after dropping the server

Dropping the server (see `old-server.md`) removed the server-only
directories. The libraries shared with the viewer (`win-system`,
`util`, `thread`, `gui` and others) still held a lot of code that only
the server used. The `dead-code` branch removed it in five commits
(about 13,700 lines):

| Commit    | What it removed |
|-----------|-----------------|
| `630df21` | Source files that are not linked into any program |
| `d022fa8` | Functions that nothing calls |
| `716cacc` | Code that only the functions removed in `d022fa8` used, including the WTS class |
| `1187f62` | `UpdateRequestSender`, whose delay was always zero |
| `20956d9` | `DesktopSelector`, which became unlinked after `716cacc` |

The lists below record what was removed. When importing a newer
upstream TightVNC, check an upstream change against them before porting
it. A change that touches only removed code can usually be skipped. A
change that starts calling removed code may mean the code has to come
back from the git history (any commit before `630df21`).

## Procedure

### 1. Find unlinked files and discarded functions with the linker

Rebuild the whole solution in a separate worktree, with the linker in
verbose mode and whole-program optimization (LTCG) turned off. LTCG
removes unused functions inside the compiler, where the linker never
reports them. The Release configurations already compile with
function-level linking (`/Gy`) and link with `/OPT:REF`, so the linker
discards each unreferenced function separately and says so.

```powershell
git worktree add --detach ..\deadscan <branch>
cd ..\deadscan
$env:LINK = '/VERBOSE'     # link.exe appends this to its options
MSBuild SquirrelVNC.slnx /m /t:Rebuild /p:Configuration=Release /p:Platform=x64 `
    /p:WholeProgramOptimization=false /v:normal > ..\deadscan-x64.log
```

The log is about 20 MB. Attribute each line to its project by the
`N>` prefixes that MSBuild writes. Use only the links of
`SquirrelVNC.exe` (tvnviewer) and `viewer-keysym-test.exe`: code used
only by the test program is still live.

- **Unlinked files.** Lines of the form `Loaded win-system.lib(Foo.obj)`
  list each object file the linker pulls in from a library. A library
  `.cpp` file that never appears for either program is not linked, so
  nothing defined in it is used. Its header may still be included, for
  a type name or a constant, so remove the `.cpp` and `.h` together and
  then fix the includes the compiler complains about.
- **Discarded functions.** Lines of the form
  `Discarded "public: void __cdecl Foo::bar(int)" (...) from foo.lib(Foo.obj)`
  list functions dropped by `/OPT:REF`. A function counts as a candidate
  only if it is discarded from every link that loaded its object file.
  Ignore names in `std::`, compiler-generated symbols (with a backquote,
  such as vftables) and lambdas, and symbols discarded from more than
  one object file (those are inline functions from headers).

Also look for files that no project lists, and headers that nothing
includes. `gui/TabContainer.cpp` was in git but in no `.vcxproj`.

### 2. Filter out functions that were only inlined

The discarded list is mostly false positives: about 620 functions on
the first pass. The compiler inlines many small functions into every
caller, and the linker then drops the out-of-line copy. Examples are
`DesktopWindow::onKey` and every dialog button handler. To filter them,
strip comments and count how often each function's name occurs as a
word in the source:

- Keep a candidate only if the name appears no more than twice per
  overload, which means only its declaration and its definition.
- Free (non-member) functions often have no declaration in a header.
  For those, two occurrences can mean a definition and one call (for
  example `isDoubleDeadCharacters` in `RfbKeySym.cpp`), so check each
  one by hand.

This filter misses some dead functions: a method whose name is shared
with another class (for example `Environment::getCurrentUserName`
versus `WTS::getCurrentUserName`) appears more than twice. Removing
code also orphans other code. So repeat steps 1 to 3 until a pass finds
nothing new; here it took three linker passes.

### 3. Remove, then let the compiler decide

Delete each candidate's definition and declaration. Also delete the
comment that belongs to it: the comment directly above it, or a
TightVNC-style `//`-boxed comment separated from it by one blank line.
Then rebuild Release x64 and Win32 with `/t:Rebuild`.

- A compile error means a false positive: restore that item. The only
  one here was `util/ZlibException.h`, which is included as
  `ZLibException.h`, so a case-sensitive search had missed it.
- Compare the warnings with the previous full rebuild
  (`warn-diff.ps1`). Apart from shifted line numbers, the only
  difference should be warnings that disappear along with deleted code.
- Read the diff, especially the headers. Look for orphaned comments,
  duplicate `protected:` lines, empty sections, and `friend`
  declarations naming removed classes.

### Limitations

- **Unused virtual functions** are not found. A class's vtable refers to
  all of its virtual functions, so the linker keeps them.
- **Inline functions defined in headers** are never compiled into an
  object file when unused, so the linker never reports them.
- **Settings that are always set to one value** are not found. For
  example, `RemoteViewerCore::m_forceFullUpdate` is always false and
  `m_isTightEnabled` is always true. Removing those means simplifying
  the code around them, not just deleting it, so they were left alone.
  `UpdateRequestSender` (`1187f62`) was the one case simplified by hand.
- **Data members** written only by removed functions were removed by
  hand where noticed, not searched for systematically.
- The linker analysis was run for x64 only; the Win32 build was only
  compiled. Code under `#ifdef _WIN64` or `#ifndef _WIN64` might be
  judged differently on Win32.

## Removed files

Each name is a class, or a set of functions, whose `.cpp` and `.h` were
deleted (only the `.h`, for header-only files). All are relative to the
repository root.

- **file-lib:** FileNotFoundException
- **ft-common:** FileTransferException, WinFilePath
- **gui:** BalloonTip, CommonControlsEx, FilteredTextBox, ImagedButton,
  ListBox, MessageWindow, StringFilter, Tab, TabContainer (`.cpp` was in
  no project), TabControl, ThemeLib, Tooltip, WindowFinder,
  WindowMessageHandler (`.h` only); `drawing/`BitmapGraphics,
  `drawing/`Pen
- **io-lib:** ByteArrayOutputStream, DataCopy
- **network:** TcpClientThread
- **region:** RectSerializer
- **thread:** DesktopSelector, GlobalMutex, GuiThread, ThreadCollector,
  ZombieKiller
- **util:** AnEventListener (`.h` only), BrokenHandleException, Command,
  CommandLine, Deflater, DemandTimer, KeyContainer, MacroCommand, md5,
  MemUsage, StringVector (`.h` only)
- **viewer-core:** UpdateRequestSender
- **win-system:** AnonymousPipe, AnonymousPipeFactory,
  ApplicationCrashEvents (`.h` only), AutoImpersonator,
  CtrlAltDelSimulator, CurrentConsoleProcess, DynamicLibrary,
  EmulatedAnonymousPipeFactory, Impersonator, InputInjector, Keyboard,
  LocalWindowsApplication, NamedPipe, ParentProcess, Pipe, PipeClient,
  PipeImpersonatedThread, PipeServer, ProcessHandle, SCMClient,
  SecurityAttributes, Service, SharedMemory, UipiControl, WinCursor,
  WindowsDisplays, WindowsEventEx, WinHandles, WinStaLibrary,
  WinTimeMillis, Workstation, WTS; `security/`SecurityDescriptor,
  `security/`SecurityIdentifier

`rfb/EncodingDefs.cpp` and `rfb/TunnelDefs.cpp` are also unlinked, but
were kept. The viewer uses the numeric codes in their headers. The
`.cpp` files define the Tight capability signature strings (`"TIGHT___"`
and so on), which are kept as a protocol reference.

## Removed functions in remaining files

Listed by the file holding the definition. The declarations were
removed from the matching header.

- **config-lib/ConnectionHistory:** getLimit
- **config-lib/IniFileSettingsManager:** setPathToFile
- **file-lib/File:** canRead, canWrite, getFileExtension
- **file-lib/WinFile:** getPathName
- **ft-client-lib/FileTransferReplyBuffer:** getDownloadFileFlags
- **ft-client-lib/FileTransferRequestSender:** sendCompressionSupportRequest
- **ft-client-lib/OperationSupport:** isMD5Supported, isDirSizeSupported
- **ft-common/FileInfo:** isExecutable, getFlags
- **gui/BaseDialog:** setDefaultPushButton
- **gui/BaseWindow:** enableWindow, updateWindow, setClassStyle,
  setClassBackground, setClassMenu, setTimer, killTimer
- **gui/ComboBox:** deleteItem
- **gui/Control:** setTextVerticalAlignment, getTextVerticalAlignment,
  hasFocus, isExStyleEnabled
- **gui/ListView:** removeItem, selectItem, allowMultiSelection
- **gui/Menu:** getWindowMenu, getSubMenu, termMenu, appendSubMenu,
  insertCheckMenuItem, insertSubMenu, deleteMenu, modifyMenu, setMenuItem
- **gui/NotifyIcon:** getIcon, showBalloon
- **gui/ScrollBar:** enableVirtualScroll, disableVirtualScroll
- **gui/SpinControl:** setAccel, autoAccelerationHandler,
  enableAutoAcceleration
- **gui/TextBox:** getTextLengthLimit, setTextLengthLimit,
  getCurrentLineIndex, getLineCount, getCaretPos, setCaretPos,
  selectText, showBalloonTip
- **gui/ToolBar:** addBitmap, addSystemBitmap, addButton, addNButton,
  pressButton, setButtonsSize, getButtonsWidth, getTotalWidth
- **gui/drawing/DeviceContext:** selectObject
- **gui/drawing/Graphics:** setBkMode, setBkColor, setTextColor,
  setBrush, setPen, moveTo, lineTo, drawBitmap, drawText
- **gui/drawing/Bitmap, DeviceContext:** `friend class BitmapGraphics`
- **log-writer/FileAccount:** isTheOurFileName
- **log-writer/LogWriter:** isDebug
- **network/TcpServer:** getBindHost
- **network/socket/SocketAddressIPv4:** free function getLocalIPAddrString
- **network/socket/SocketIPv4:** isBound, getLocalAddr, getPeerAddr,
  getSocketOptions
- **rfb/AuthDefs:** SecurityDefs::convertFromAuthType,
  AuthDefs::convertFromSecurityType
- **rfb/CursorShape:** assignMaskFromWindows, resetToEmpty
- **rfb/PixelConverter:** getSrcBitsPerPixel, getDstBitsPerPixel (both
  virtual, with no overrides)
- **rfb/StandardPixelFormatFactory:** create16bppPixelFormat,
  create6bppPixelFormat, create3bppPixelFormat
- **tvnviewer/LoginDialog:** getServerHost
- **tvnviewer/ResourceStrings:** getStrPureRes
- **tvnviewer/ScaleManager:** getViewedRect, getScaledRect, keepAspectRatio
- **tvnviewer/ViewerWindow:** doCommand
- **util/AnsiStringStorage:** checkAnsiConversion
- **util/Keymap:** keySymToVirtualCode, keySymToUnicodeChar, and the
  free function keysym2ucs (keysym-to-character, the server's direction;
  the viewer converts the other way)
- **util/ResourceLoader:** loadStandartIcon
- **util/StringParser:** parseUInt64, parseByte, parseByteHex
- **util/StringStorage:** quoteSelf, split, toUpperCase, replaceChar
- **util/VncPassCrypt:** updatePlain, challengeAndResponseIsValid
- **viewer-core/CapsContainer:** isAdded, getInfo, getDescription
- **viewer-core/RemoteViewerCore:** forceFullUpdateRequests,
  deferUpdateRequests, enableTightSecurityType, getDesktopSize; members
  m_updateTimeout and m_updateRequestSender. `sendFbUpdateRequest` now
  always sends the request directly. It always did, because the
  deferral timeout was never set.
- **viewer-core/TightDecoder:** transformPixelToTight
- **win-system/Environment:** getCurrentModuleFolderPath,
  isItTheSamePathAsCurrent, getCurrentUserName, getComputerName,
  isWinNTFamily, isWin2000, isWinXP, isWin2003Server, isVistaOrLater,
  isWin7, simulateCtrlAltDel, simulateCtrlAltDelUnderVista, isAeroOn,
  init, member m_osVerInfo. What remains is getErrStr,
  getSpecialFolderPath and getCurrentModulePath.
- **win-system/Process:** setStandardIoHandles, setHandleInheritances,
  waitForExit, stopWait, getExitCode, getProcessHandle
- **win-system/Registry:** getCurrentLocalMachineKey
- **win-system/RegistryKey:** getHKEY, createSubKey
- **win-system/Screen:** getDesktopDimension, getVisibleMonitorCount
- **win-system/Shell:** runAsAdmin
- **win-system/SystemException:** getErrorCode, getSystemErrorDescription
- **win-system/SystemInformation:** isSupportStretchBlt, getBitsPixel,
  getMonitorCount
- **win-system/WindowsApplication:** removeModelessDialog
- **thread/Thread.h:** the include of DesktopSelector.h

Kept on purpose, although unused: DataInputStream::readInt8/readInt64
and DataOutputStream::writeInt8/writeInt16/writeInt64, so that the
stream classes stay complete.

To regenerate the lists exactly from git:

```sh
git diff --name-status 630df21~1 20956d9 | grep '^D'      # removed files
# Lines that start a removed definition in kept files (roughly; also some
# return types written on their own line):
git diff -U0 --diff-filter=M 630df21~1 20956d9 -- '*.cpp' | grep '^-[^-/ {}#	]'
```

## Later removals

- **win-system/Process** (both files), whose only user was the button
  that opens the log folder in the Configuration dialog. That button ran
  `explorer` through `CreateProcess()`, which searched the viewer's own
  directory for it first. `4638a6c` switched it to
  `Shell::showInFolder()`, and the commit that adds this entry removed
  the class.
- **`ToolBar::create()`**, a second way of creating the toolbar window,
  which nothing called: the viewer uses `attachToolBar()`. It was found
  through its 64-bit cast warning (C4312), and removing it was simpler
  than fixing the cast.
