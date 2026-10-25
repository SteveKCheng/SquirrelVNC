# Raw-pointer parameters that could be references

A survey (2026-10-25) of which functions take a raw pointer to an object
that they only borrow: the pointee must be non-null, it is used only
during the call, and the pointer is not kept after the call returns.
Such parameters could be `T &` or `const T &`, which documents both
facts in the type. **Preliminary:** the
lists below come from a tool with known blind spots (see the caveats),
so check each function when converting it.

## Scope

Counted: parameters whose type is a pointer to an object (after
resolving typedefs), in code under `core-lib`, `win32-lib`, `tvnviewer`,
`harnesses` and `tools` (the third-party `x11region.c` is left out).

Not counted:

- typedefs that are pointers but are not spelt as pointers: `HWND`,
  `HANDLE`, `HDC`, `LPARAM`, `LPCTSTR` and so on;
- strings: pointers to `char`, `wchar_t` or `TCHAR`;
- function pointers;
- `void *` buffers, pointers to integers (out-parameters and arrays
  of them), and `argv`. They are listed at the end, briefly.

Owning pointers are mostly `std::unique_ptr` already, after the
earlier clean-up.

## How it was found

With `clang-query` (from the Visual Studio LLVM tools, see
`clang-tidy.md`) on the Debug x64 compilation database, over all
213 sources:

1. every parameter whose canonical type is a pointer;
2. how each one is used in its function's body:
   - **kept**: used directly as a member initialiser, as the right-hand
     side of an assignment, as a local variable's initialiser, as a
     return value, or captured by a lambda;
   - **array**: subscripted, or in pointer arithmetic;
   - **null-checked**: compared with null, or converted to `bool`;
   - **deleted**;
3. where each one is passed on, as an argument to another function's
   parameter. A parameter passed on to another parameter that is kept
   or null-checked counts as kept or null-checked too (repeated until
   nothing changes). An argument to the constructor of a local
   variable (`AutoLock al(p)`, `Graphics g(dc)`) does not count as
   kept, since the object goes when the call returns;
4. callers that pass a null literal, and parameters with a null
   default argument (`= nullptr`), which make it nullable;
5. call-site arguments that are not obviously non-null (not `&x`,
   `this`, or a parameter passed on), read by hand.

Declarations and definitions are matched by file, function name,
position and type of the parameter. Virtual functions are grouped by
name, position and type of the parameter, so that a base declaration and its overrides are
judged together.

## Plan: `const` pointees first

Decided 2026-10-25: convert the candidates whose pointee is `const`
first (243 of the 507, counting each override separately), since that
is the least controversial change with the most effect on
readability. The other kinds (non-`const` pointees, out-parameters,
arrays, nullable or kept pointers) are decided later, possibly
piecemeal as code is touched for other reasons, since many need
special treatment.

Decisions:

- **No boilerplate documentation.** A `const T &` parameter says that
  the argument is not null and is only borrowed, so converted functions
  do not get `@pre` comments repeating that. `@pre` and `@post` are
  added only for other conditions found while converting.
- **Checks:** the build of the three usual configurations, the tests,
  and review of the diff. Comparing the machine code before and after
  (`dumpbin /disasm` of the Release `.obj` files) is provisional, done
  by sampling only: references and pointers can be optimised slightly
  differently, so differences do not by themselves mean a mistake.

### Tool

A script driven by `clang-query` (as in the survey), given the
parameters to convert. It edits the source at the exact locations
`clang-query` reports:

- declarations and definitions, overrides included: `const T *p`
  becomes `const T &p`;
- in the body: `p->` becomes `p.`, `*p` becomes `p`, and `p` passed on
  to another converted parameter is unchanged;
- at call sites: `&x` becomes `x`; any other argument `e` becomes `*e`
  (parenthesised unless simple);
- any other use of `p` (comparison, `sizeof`, passed to a parameter
  not being converted) is reported for editing by hand, not guessed.

The compiler checks the rest: a pointer and a reference do not convert
into each other, so a missed edit fails to build.

### Batches

Pointers are only ever passed on between `const` candidates of the
same type, never to parameters that stay pointers, so the candidates
fall into closed chains (92 parameters that pass the pointer nowhere,
30 small chains, and two big ones: 22 parameters of
`const FrameBuffer *` and 42 of `const Rect *`). Each chain converts
in one commit without temporary `&`/`*` at its edges. In order:

1. **Trial:** the `const DateTime *` chain in `win32-lib/log-writer`
   (`FileAccount::format`/`print`/`flush`, `LogEntry`,
   `LogDump::updateLogHeaderLines`/`updateLogDumpLines`/`flush`): 7
   parameters, but every kind of edit, including a virtual family and
   a constructor.
2. **Preparation:** make `Rect(const RECT *)` and
   `Dimension(const Rect *)` `explicit`, and replace
   `Rect(const Rect *)`, which duplicates the copy constructor, with
   the copy constructor, before converting anything that could change
   overload resolution.
3. **No caveats, not virtual:** one commit per module (`region`,
   `rfb`, `viewer-core`, `win32-lib`, `tvnviewer`), with their small
   chains.
4. **The two big chains:** `const FrameBuffer *`, then `const Rect *`.
5. **Other virtual families:** the `CoreEventsAdapter` callbacks, the
   rest of `FrameBuffer`'s virtual functions, and so on.

Then one commit per caveat, so that the reasoning for each is in one
place:

6. call sites whose arguments are not obviously non-null (8
   parameters), with the reason each cannot be null;
7. pointers passed to a local object's constructor (9 parameters);
8. pointers passed through `std::make_unique` (4 parameters), with
   the constructor they reach;
9. overloaded functions not already covered, checking which overload
   each call resolves to;
10. functions in `harnesses/` and `tools/` only.

### Progress

Done (2026-10-25), each building in the three configurations and
passing the tests:

| Commit | What |
|---|---|
| `e53e96b` | Trial: the `const DateTime *` chain in `log-writer` |
| `7d79f8f`, `b6cdefb` | Preparation: `Rect(const Rect *)` removed; `Rect(const RECT *)` and `Dimension(const Rect *)` made `explicit` |
| `570d24c` | `core-lib/region` (13 parameters) |
| `c7dac63` | `core-lib/rfb`: only `PixelFormat::isEqualTo` was live |
| `e96a137` | `core-lib/viewer-core` (27 parameters) |
| `7c615d0` | `win32-lib` (19 parameters) |
| `1c924cd` | `tvnviewer` (4 parameters) |
| `847de7b` | The `const FrameBuffer *` chain (23 parameters, with its virtual families) |
| `a8a78ac` | The `const Rect *` chain (41 parameters, with its virtual families) |
| `2a9b3f2` | `Rect::setRect` and `ScaleManager::calcScaled`, left over from the `Rect` chain |
| `c1c63c4` | Leftovers in `ft-common` and `ft-client-lib` (`FileInfo(const File &)`, now `explicit`) |
| `935e63f` | `CoreEventsAdapter`'s callbacks, with their overrides and what they pass on to |
| `338ede0` | `FrameBuffer`'s virtual setters, `CursorShape`'s and `Dimension::setDim` |
| `03ddfca` | Call sites: `ConnectionData::setPlainPassword` (and `AuthenticationDialog::getPassword()` returns a reference) |
| `58d8e91` | Arguments copied into local objects (`drawImage`, `crop`, `sendPointerEvent`, ...) |
| `52475cc`, `e883c03` | Through `std::make_unique`: `DibSection` and its chain, and `ViewerInstance` with `TvnViewer::runInstance` |
| `754e1d3` | Overloads: `StringStorage::isEqualTo`, `ConnectionData::setHost` and the host name of `newConnection` |
| `2bb92fe` | Functions in the test harnesses |

**The `const` part of the plan is done.** A rerun of the analysis on
`2bb92fe` finds no `const` candidates left. The 15 parameters with
`const` pointees that remain are not candidates:

- arrays with a count (`std::span` candidates): the `const FileInfo *`
  of the file-transfer operations, `FileInfoList`'s constructor and
  `appendAfter`, and `ViewerCmdLine::processCmdLine`'s options;
- nullable: `FileInfoList::setChild` (called with null and a count of
  0), `ConnectionData::setCryptedPassword` and `BaseWindow::redraw`
  (null for the whole window).

Found along the way and committed separately: `a4ca831` (the
swapped parameter names of `stretchFromDibSection`), `04c19b6` (the
discarded `Rect::intersection` results in `DesktopWindow::repaint`,
and `[[nodiscard]]`), and dead code removed instead of converted
(`1fb6910` `PixelConverter`, `b323ad2` `CursorShape::clone`,
`579efa6` `FrameBuffer::copyFromRotated*`; see `dead-code.md`).

Lessons for the remaining batches:

- Check for an existing overload that takes a reference:
  `Region::add` had both, and the rewrite made two identical functions
  (the build catches it).
- A call through `std::make_unique` is invisible to the script, since
  the argument is passed on inside the standard library; convert such
  constructors in the `make_unique` batch (`DibSection`,
  `ViewerInstance`).
- Read a function's callers before converting it: three of the rfb
  candidates had none.
- Rerun the whole analysis before each batch. The chains change as
  code is removed or converted: functions drop out of a big chain and
  become chains of their own (`setRect`, `calcScaled`), and the line
  numbers in the survey go stale.
- `make_unique` calls show up as "other use" problems in the script's
  output: they need no edit once the forwarding parameter is a
  reference, but a `make_unique` that passes `&x` must be edited by hand
  (the compiler finds it).
- The script needed two fixes on the way: a `*` inside template
  arguments (`std::map<UINT32, AuthHandler *> *`), and a parameter
  whose name is commented out (`const FrameBuffer * /* fb */`).

Moved from the module batches to the caveat batches:
`StringStorage::isEqualTo` and the `ConnectionData::setHost` /
`TvnViewer::newConnection` chain (overloads), and the constructors of
`DibSection` and `ViewerInstance` (`make_unique`).

### Non-`const` pointees: objects operated on

Decided 2026-10-25: a non-`const` pointer to an object that the
function operates on (not a value-like object that it fills in or
replaces, which is an out-parameter) is converted like the `const`
ones, as `string.resize()` would be written rather than
`(&string)->resize()`. Done:

| Commit | What |
|---|---|
| `cdc1cf7` | The decoders' `FrameBuffer *` and `RfbInputGate *` |
| `6024835` | The client messages' `RfbOutputGate *` (`send`, `sendUtf8`) |
| `0e85ad5` | `SettingsManager *` (`loadFromStorage`, `saveToStorage`; `ViewerSettingsManager::getInstance()` returns a reference) and `CapabilitiesManager *` (`AuthHandler::addAuthCapability`) |

Left as pointers on purpose:

- `DataInputStream *` and `DataOutputStream *`, to stay consistent with
  the file-transfer code, which is not being changed (it is
  TightVNC-specific and hard to test). For the same reason
  `FileTransferCapability::addCapabilities` keeps its
  `CapabilitiesManager *`.
- `AutoLock`, `LocalMutex *` and `FbUpdateNotifier *`: the custom
  threading and locks are to be replaced.
- `DeviceContext *`, `TextBox *`, `MSG *` and other GUI types: the GUI
  code passes Win32-style pointers throughout.
- `onConnected(RfbOutputGate *)`: `ViewerWindow` passes it on to file
  transfer, which keeps it.
- Out-parameters (`StringStorage *`, `std::vector<...> *`, `Rect *`
  results and so on), still to be decided; many would read better
  returning the value.

## Results

Of 662 object-pointer parameters (counting a function's declaration
and definition once, and each override separately):

| | Count |
|---|---|
| Borrowed, never null-checked: candidates | 507 |
| Kept after the call, or used as an array | 141 |
| Null-checked, or given null by some caller | 13 |
| Unclear (stored as an `LPARAM`) | 1 |

By area, the candidates are 320 in `core-lib`, 110 in `win32-lib`,
53 in `tvnviewer`, 21 in `harnesses` and 3 in `tools`.

The most common types:

| Type | Count | Notes |
|---|---|---|
| `DataInputStream *` | 78 | mostly the file-transfer reply handlers |
| `const Rect *` | 78 | |
| `StringStorage *` | 40 | out-parameters, nearly all of them |
| `const FrameBuffer *` | 32 | |
| `const PixelFormat *` | 30 | |
| `const StringStorage *` | 24 | |
| `FrameBuffer *` | 22 | |
| `const Dimension *` | 19 | |
| `RfbInputGate *` | 17 | the decoders |
| `RfbOutputGate *` | 10 | the client-to-server messages' `send` |
| `const Point *` | 9 | |

### Patterns worth treating differently

- **Small value types** (`const Rect *`, `const Dimension *`,
  `const Point *`, `const PixelFormat *`, `const DateTime *`) become
  `const T &`. This is most of the mechanical work, and the call sites
  lose a `&`.
- **Out-parameters** (`StringStorage *out`, `std::vector<UINT32> *`,
  `int *value` and so on) could become references, but many would be
  clearer returning the value (or `std::optional`) instead. Decide
  per function, not wholesale.
- **Arrays with a count** (`const FileInfo *` plus `size_t` in the
  file-transfer operations, `FileInfoList`) are `std::span`
  candidates, not references. `FileInfoList(const FileInfo *, size_t)`
  reads `filesInfo[0]` and computes `count - 1`, so its precondition
  `count >= 1` should be documented. All of its callers check it today
  (`setChild` returns early on 0; the dialogs need a selection).
- **Identity only**: `ListenerContainer::removeListener` and
  `WinHooks::unregisterKeyboardHook` only compare the pointer, and a
  caller passes `nullptr` to the latter. Leave them as pointers.
- **Kept, but only used during the call**:
  `FileTransferMainDialog::onFtTargetFileExists` hands its two
  `FileInfo *` to `FileExistDialog::setFilesInfo`, which keeps them in
  members for the modal dialog that follows; they dangle afterwards
  but are not used again. Converting means changing the dialog to copy
  or to take the files in `showModal`.
- **Implicit conversion from a pointer**: `Rect(const RECT *)` is not
  `explicit`, so a `const RECT *` converts to a `Rect` silently.
  Taking `const RECT &` (and adding `explicit`) fixes that too.

### Null-checked or nullable (not candidates)

These either test for null or receive null from some caller; each
needs a decision (keep the pointer, use `std::optional`, or remove
the null case):

- `FileInfoList::setChild`, `filesInfo`: passed on, a caller passes null
- `FileTransferCapability::setInterface`, `ftInterface`: a caller passes null; also kept (by `FileTransferCore::setInterface`)
- `ConnectionData::setCryptedPassword`, `password`: compared with null
- `DesktopWindow::getServerGeometry`, `rect`: compared with null
- `BaseWindow::redraw`, `rcArea`: passed on, compared with null, null default argument
- `BaseWindow::getClientRect`, `rc`: passed on, tested as bool
- `Icon::Icon`, `bitmap`: passed on to a null-checked parameter
- `Icon::Icon`, `mask`: passed on to a null-checked parameter
- `Icon::fromBitmap`, `bitmap`: compared with null
- `Icon::fromBitmap`, `mask`: compared with null
- `ResourceLoader::loadString`, `string`: compared with null
- `Environment::getSpecialFolderPath`, `out`: compared with null
- `WinHooks::unregisterKeyboardHook`, `hookEventListener`: a caller passes null

Some of these checks may only be defensive, with every caller passing
`&x` (`ResourceLoader::loadString` looks like one); those would become
candidates once the check is removed.

### Kept after the call (not candidates)

Mostly what you would expect, and they should stay pointers or become
something owning or shared: the `LogWriter *` / `Logger *` that most
classes keep; constructor parameters stored in members (`ViewerWindow`,
`DesktopWindow`, `FileTransferCore`, `RemoteViewerCore`,
`ViewerCmdLine`, `TcpConnection` and others); setters
(`setRemoteViewerCore`, `setFileTransfer`, `setInterface`,
`setParent`, `setAdapter`, `setOutput`); listener registration
(`addListener`, `registerKeyboardHook`, `addAuthCapability`,
`addServerMsgCapability`); `PixelConverter::convert`, which returns
its parameter. For many of these a reference would still document
"not null", but would hide that the pointee must outlive the object;
leave them out of a first pass.

Three virtual families are mixed, borrowed in the base but kept by one
override, so they stay pointers unless that override changes:
`onConnected` (`ViewerWindow` passes the `RfbOutputGate *` to
`FileTransferCapability::setOutput`), `onFtTargetFileExists` (see
above), and `CapabilitiesManager::addAuthCapability` /
`addServerMsgCapability` (`RemoteViewerCore` registers the handler).

## Candidates

55 virtual families (78 parameters, counting each family once) and
282 parameters of other functions, in 80 files. Parameters are named
as in the declaration; `#n` is the position in the parameter list.

### Virtual-function families

A base declaration and its overrides must change together; all members of these families are borrows.

- `addAuthCapability` (#1 `CapabilitiesManager *`): AuthHandler, VncAuthenticationHandler
- `authenticate` (#1 `DataInputStream *`, #2 `DataOutputStream *`): AuthHandler, VncAuthenticationHandler
- `getEnabledClientMsgCapabilities` (#1 `std::vector<UINT32> *`): CapabilitiesManager, RemoteViewerCore
- `getEnabledEncodingCapabilities` (#1 `std::vector<UINT32> *`): CapabilitiesManager, RemoteViewerCore
- `getEnabledServerMsgCapabilities` (#1 `std::vector<UINT32> *`): CapabilitiesManager, RemoteViewerCore
- `targetFileExists` (#1 `FileInfo *`, #2 `FileInfo *`): CopyFileEventListener, FileTransferCore
- `copy` (#1 `FrameBuffer *`, #2 `const FrameBuffer *`, #3 `const Rect *`, #4 `LocalMutex *`): CopyRectDecoder, DecoderOfRectangle
- `decode` (#1 `RfbInputGate *`, #2 `FrameBuffer *`, #3 `const Rect *`): CopyRectDecoder, DecoderOfRectangle, HexTileDecoder, RawDecoder, RreDecoder, TightDecoder, ZrleDecoder
- `onAuthError` (#1 `const AuthException *`): CoreEventsAdapter, ReplayEventsAdapter, ViewerWindow
- `onCutText` (#1 `const StringStorage *`): CoreEventsAdapter, ReplayEventsAdapter, ViewerWindow
- `onDisconnect` (#1 `const StringStorage *`): CoreEventsAdapter, ReplayEventsAdapter, ViewerWindow
- `onError` (#1 `const Exception *`): CoreEventsAdapter, ReplayEventsAdapter, ViewerWindow
- `onFrameBufferPropChange` (#1 `const FrameBuffer *`): CoreEventsAdapter, ReplayEventsAdapter, ViewerWindow
- `onFrameBufferUpdate` (#1 `const FrameBuffer *`, #2 `const Rect *`): CoreEventsAdapter, ReplayEventsAdapter, ViewerWindow
- `process` (#1 `RfbInputGate *`, #2 `FrameBuffer *`, #3 `FrameBuffer *`, #4 `const Rect *`, #5 `LocalMutex *`, #6 `FbUpdateNotifier *`): DecoderOfRectangle, RawDecoder
- `onPaint` (#1 `DeviceContext *`, #2 `PAINTSTRUCT *`): DesktopWindow, PaintWindow
- `assignProperties` (#1 `const FrameBuffer *`): DibFrameBuffer, FrameBuffer
- `clone` (#1 `const FrameBuffer *`): DibFrameBuffer, FrameBuffer
- `cmpFrom` (#1 `const Rect *`, #2 `const FrameBuffer *`): DibFrameBuffer, FrameBuffer
- `copyFrom` (#1 `const FrameBuffer *`, #1 `const Rect *`, #2 `const FrameBuffer *`): DibFrameBuffer, FrameBuffer
- `fillRect` (#1 `const Rect *`): DibFrameBuffer, FrameBuffer
- `isEqualTo` (#1 `const FrameBuffer *`): DibFrameBuffer, FrameBuffer
- `move` (#1 `const Rect *`): DibFrameBuffer, FrameBuffer
- `overlay` (#1 `const Rect *`, #2 `const FrameBuffer *`): DibFrameBuffer, FrameBuffer
- `setDimension` (#1 `const Dimension *`, #1 `const Rect *`): DibFrameBuffer, FrameBuffer
- `setEmptyDimension` (#1 `const Rect *`): DibFrameBuffer, FrameBuffer
- `setEmptyPixelFmt` (#1 `const PixelFormat *`): DibFrameBuffer, FrameBuffer
- `setPixelFormat` (#1 `const PixelFormat *`): DibFrameBuffer, FrameBuffer
- `setProperties` (#1 `const Dimension *`, #1 `const Rect *`, #2 `const PixelFormat *`): DibFrameBuffer, FrameBuffer
- `setPropertiesWithoutResize` (#1 `const Dimension *`, #2 `const PixelFormat *`): DibFrameBuffer, FrameBuffer
- `onDirSizeReply` (#1 `DataInputStream *`): DownloadOperation, FileTransferEventAdapter, FileTransferEventHandler, FileTransferReplyBuffer
- `onDownloadDataReply` (#1 `DataInputStream *`): DownloadOperation, FileTransferEventAdapter, FileTransferEventHandler, FileTransferReplyBuffer
- `onDownloadEndReply` (#1 `DataInputStream *`): DownloadOperation, FileTransferEventAdapter, FileTransferEventHandler, FileTransferReplyBuffer
- `onDownloadReply` (#1 `DataInputStream *`): DownloadOperation, FileTransferEventAdapter, FileTransferEventHandler, FileTransferReplyBuffer
- `onLastRequestFailedReply` (#1 `DataInputStream *`): DownloadOperation, FileTransferEventAdapter, FileTransferEventHandler, FileTransferReplyBuffer, RemoteFileListOperation, RemoteFileRenameOperation, RemoteFilesDeleteOperation, RemoteFolderCreateOperation, UploadOperation
- `onFileListReply` (#1 `DataInputStream *`): DownloadOperation, FileTransferEventAdapter, FileTransferEventHandler, FileTransferReplyBuffer, RemoteFileListOperation, RemoteFilesDeleteOperation, UploadOperation
- `flush` (#3 `const DateTime *`): FileAccount, LogDump
- `onServerMessage` (#2 `DataInputStream *`): FileTransferCapability, ServerMessageListener
- `ftOpErrorMessage` (#1 `FileTransferOperation *`): FileTransferCore, OperationEventListener
- `ftOpFinished` (#1 `FileTransferOperation *`): FileTransferCore, OperationEventListener
- `ftOpInfoMessage` (#1 `FileTransferOperation *`): FileTransferCore, OperationEventListener
- `ftOpStarted` (#1 `FileTransferOperation *`): FileTransferCore, OperationEventListener
- `onCompressionSupportReply` (#1 `DataInputStream *`): FileTransferEventAdapter, FileTransferEventHandler, FileTransferReplyBuffer
- `onMd5DataReply` (#1 `DataInputStream *`): FileTransferEventAdapter, FileTransferEventHandler, FileTransferReplyBuffer
- `onMvReply` (#1 `DataInputStream *`): FileTransferEventAdapter, FileTransferEventHandler, FileTransferReplyBuffer, RemoteFileRenameOperation
- `onRmReply` (#1 `DataInputStream *`): FileTransferEventAdapter, FileTransferEventHandler, FileTransferReplyBuffer, RemoteFilesDeleteOperation
- `onMkdirReply` (#1 `DataInputStream *`): FileTransferEventAdapter, FileTransferEventHandler, FileTransferReplyBuffer, RemoteFolderCreateOperation, UploadOperation
- `onUploadDataReply` (#1 `DataInputStream *`): FileTransferEventAdapter, FileTransferEventHandler, FileTransferReplyBuffer, UploadOperation
- `onUploadEndReply` (#1 `DataInputStream *`): FileTransferEventAdapter, FileTransferEventHandler, FileTransferReplyBuffer, UploadOperation
- `onUploadReply` (#1 `DataInputStream *`): FileTransferEventAdapter, FileTransferEventHandler, FileTransferReplyBuffer, UploadOperation
- `hasPixelFormatOf` (#1 `const PixelFormat *`, #2 `const FrameBuffer *`): FrameBuffer
- `getString` (#2 `StringStorage *`): IniFileSettingsManager, RegistrySettingsManager, SettingsManager
- `getPassword` (#1 `StringStorage *`): ReplayAuthHandler, ViewerVncAuthHandler, VncAuthenticationHandler
- `send` (#1 `RfbOutputGate *`): RfbClientToServerMessage, RfbCutTextEventClientMessage, RfbFramebufferUpdateRequestClientMessage, RfbKeyEventClientMessage, RfbPointerEventClientMessage, RfbSetEncodingsClientMessage, RfbSetPixelFormatClientMessage
- `registerWindowClass` (#1 `WNDCLASS *`): TvnViewer, WindowsApplication

### Other functions, by file

- `core-lib/ft-client-lib/FileInfoList.cpp`: `getAbsolutePath` (`storage`)
- `core-lib/ft-client-lib/FileTransferCore.cpp`: `updateSupportedOperations` (`clientCaps`, `serverCaps`)
- `core-lib/ft-client-lib/FileTransferMessageProcessor.cpp`: `processRfbMessage` (`input`)
- `core-lib/ft-client-lib/FileTransferOperation.cpp`: `getLocalPath` (`currentFile`, `out`), `getRemotePath` (`currentFile`, `out`)
- `core-lib/ft-client-lib/FileTransferReplyBuffer.cpp`: `getLastErrorMessage` (`storage`), `readCompressedDataBlock` (`input`)
- `core-lib/ft-client-lib/LocalFilesDeleteOperation.cpp`: `deleteFile` (`file`)
- `core-lib/ft-common/FileInfo.cpp`: `FileInfo` (`file`)
- `core-lib/io-lib/DataInputStream.cpp`: `readUTF8` (`storage`)
- `core-lib/region/Dimension.h`: `Dimension` (`r`), `setDim` (`rect`), `cmpDim` (`dim`), `isEqualTo` (`other`), `intersection` (`other`)
- `core-lib/region/Point.h`: `isEqualTo` (`other`)
- `core-lib/region/Rect.h`: `Rect` (`rect`, `rect`), `setRect` (`rect`), `isFullyContainRect` (`inner`), `fromWindowsRect` (`winRect`), `isEqualTo` (`rect`), `intersection` (`other`)
- `core-lib/region/Region.cpp`: `set` (`src`), `addRect` (`rect`), `add` (`other`), `subtract` (`other`), `intersect` (`other`), `crop` (`rect`), `equals` (`other`), `getRectVector` (`dst`), `getRectList` (`dst`)
- `core-lib/region/x11region.c`: `miRegionOp` (`pOverlap`), `miIntersectO` (`pOverlap`), `miUnionO` (`pOverlap`), `miRegionValidate` (`pOverlap`), `miSubtractO` (`pOverlap`)
- `core-lib/rfb/CursorShape.cpp`: `clone` (`srcCursorShape`), `setDimension` (`newDim`), `setPixelFormat` (`pixelFormat`), `setProperties` (`newDim`, `pixelFormat`)
- `core-lib/rfb/FrameBuffer.cpp`: `clipRect` (`dstRect`, `srcFrameBuffer`, `srcBufferRect`, `dstClippedRect`, `srcClippedRect`), `overlayT` (`dstRect`, `srcFrameBuffer`), `copyFromRotated90` (`dstRect`, `srcFrameBuffer`), `copyFromRotated180` (`dstRect`, `srcFrameBuffer`), `copyFromRotated270` (`dstRect`, `srcFrameBuffer`)
- `core-lib/rfb/PixelConverter.cpp`: `convert` (`rect`, `dstFb`, `srcFb`), `setPixelFormats` (`dstPf`, `srcPf`), `fillHexBitsTable` (`dstPf`, `srcPf`), `fill32BitsTable` (`dstPf`, `srcPf`)
- `core-lib/rfb/PixelFormat.cpp`: `isEqualTo` (`pf`)
- `core-lib/rfb/RfbKeySym.cpp`: `vkCodeToString` (`res`)
- `core-lib/viewer-core/CapsContainer.cpp`: `add` (`capinfo`), `enable` (`capinfo`)
- `core-lib/viewer-core/CursorPainter.cpp`: `updatePointerPos` (`position`), `setNewCursor` (`hotSpot`, `cursor`, `bitmask`), `getUpperLeftPoint` (`position`)
- `core-lib/viewer-core/DecoderOfRectangle.cpp`: `notify` (`fbNotifier`, `rect`)
- `core-lib/viewer-core/FbUpdateNotifier.cpp`: `onUpdate` (`rect`), `updatePointerPos` (`position`), `setNewCursor` (`hotSpot`, `cursor`, `bitmask`)
- `core-lib/viewer-core/FileTransferCapability.cpp`: `addCapabilities` (`capabilitiesManager`)
- `core-lib/viewer-core/JpegDecompressor.cpp`: `decompress` (`dstRect`)
- `core-lib/viewer-core/RemoteViewerCore.cpp`: `setPixelFormat` (`viewerPixelFormat`), `sendPointerEvent` (`position`), `sendCutTextEvent` (`cutText`), `readSecurityTypeList` (`secTypes`), `selectSecurityType` (`secTypes`, `authHandlers`), `setFbProperties` (`fbDimension`, `fbPixelFormat`), `processPseudoEncoding` (`rect`), `copyFrameBuffer` (`dst`)
- `core-lib/viewer-core/RfbCutTextEventClientMessage.cpp`: `RfbCutTextEventClientMessage` (`cutText`), `sendUtf8` (`output`)
- `core-lib/viewer-core/RfbPointerEventClientMessage.cpp`: `RfbPointerEventClientMessage` (`position`)
- `core-lib/viewer-core/RfbSetEncodingsClientMessage.cpp`: `RfbSetEncodingsClientMessage` (`encodings`)
- `core-lib/viewer-core/RfbSetPixelFormatClientMessage.cpp`: `RfbSetPixelFormatClientMessage` (`pixelFormat`)
- `core-lib/viewer-core/TightDecoder.cpp`: `readTightPixel` (`input`), `readCompactSize` (`input`), `processJpeg` (`input`, `frameBuffer`, `dstRect`), `processBasicTypes` (`input`, `frameBuffer`, `dstRect`), `readPalette` (`input`), `readTightData` (`input`), `readCompressedData` (`input`), `drawPalette` (`fb`, `dstRect`), `drawTightBytes` (`fb`, `pixels`, `dstRect`), `drawJpegBytes` (`fb`, `pixels`, `dstRect`), `drawGradient` (`fb`, `dstRect`), `getRawTightColor` (`pxFormat`), `fillRawComponents` (`pxFormat`)
- `core-lib/viewer-core/VncAuthentication.cpp`: `vncAuthenticate` (`input`, `output`, `password`)
- `core-lib/viewer-core/ZrleDecoder.cpp`: `readAndInflate` (`input`), `getMaxSizeOfRectangle` (`dstRect`), `readType` (`input`), `readRunLength` (`input`), `readPalette` (`input`, `palette`), `readRawTile` (`input`, `tileRect`), `readSolidTile` (`input`, `tileRect`), `readPackedPaletteTile` (`input`, `tileRect`), `readPlainRleTile` (`input`, `tileRect`), `readPaletteRleTile` (`input`, `tileRect`), `drawTile` (`fb`, `tileRect`, `pixels`)
- `harnesses/rfb-fuzz/FuzzSession.cpp`: `setFrameBuffer` (`dimension`, `pixelFormat`), `readPseudoEncoding` (`rect`)
- `harnesses/rfb-replay/FrameBufferPpm.cpp`: `frameBufferToPpm` (`fb`)
- `harnesses/rfb-replay/ReplayEventsAdapter.cpp`: `addEvent` (`text`)
- `harnesses/rfb-replay/rfb-replay.cpp`: `parseOptions` (`options`), `readFile` (`data`)
- `harnesses/viewer-keysym-test/KeySymTest.cpp`: `readLine` (`line`), `removeComments` (`line`, `extractedComment`), `getWord` (`line`, `word`)
- `tools/svg2ico/svg2ico.cpp`: `getViewBox` (`document`), `getLength` (`root`), `encodePng` (`bitmap`)
- `tvnviewer/AuthenticationDialog.cpp`: `setHostName` (`hostname`)
- `tvnviewer/ConfigurationDialog.cpp`: `testNum` (`tb`)
- `tvnviewer/ConnectionData.cpp`: `setHost` (`host`), `setPlainPassword` (`password`), `getReducedHost` (`strHost`)
- `tvnviewer/DesktopWindow.cpp`: `setClipboardData` (`strText`), `doDraw` (`dc`), `drawBackground` (`dc`, `rcMain`, `rcImage`), `drawImage` (`src`, `dst`), `updateFramebuffer` (`framebuffer`, `dstRect`), `setNewFramebuffer` (`framebuffer`), `repaint` (`repaintRect`), `sendPointerEvent` (`position`), `sendCutTextEvent` (`cutText`)
- `tvnviewer/FileExistDialog.cpp`: `updateGui` (`fileInfo`, `sizeLabel`, `modTimeLabel`)
- `tvnviewer/FileTransferMainDialog.cpp`: `getSelectedFilesInfo` (`listView`), `getPathToCurrentLocalFolder` (`out`), `getPathToParentLocalFolder` (`out`), `getPathToCurrentRemoteFolder` (`out`), `getPathToParentRemoteFolder` (`out`)
- `tvnviewer/LoginDialog.h`: `setConConf` (`conConf`)
- `tvnviewer/NewFolderDialog.cpp`: `getFileName` (`storage`)
- `tvnviewer/OptionsDialog.cpp`: `getEncodingOfItem` (`comboBox`)
- `tvnviewer/ScaleManager.cpp`: `setWindow` (`rcWnd`), `calcScaled` (`rcViewed`), `getDestinationRect` (`rcDestination`), `getSourceRect` (`rcSource`), `getWndFromScreen` (`screen`, `wnd`)
- `tvnviewer/TvnViewer.cpp`: `runInstance` (`conData`, `config`), `newConnection` (`hostName`, `conData`, `connectionConfig`)
- `tvnviewer/ViewerInstance.cpp`: `ViewerInstance` (`condata`, `conConf`)
- `win32-lib/client-config-lib/ConnectionConfig.cpp`: `saveToStorage` (`sm`), `loadFromStorage` (`sm`)
- `win32-lib/client-config-lib/ViewerConfig.cpp`: `loadFromStorage` (`storage`), `saveToStorage` (`storage`), `getLogDir` (`logDir`)
- `win32-lib/config-lib/IniFileSettingsManager.cpp`: `readFile` (`data`)
- `win32-lib/file-lib/File.cpp`: `getName` (`name`), `getPath` (`pathname`), `list` (`fileList`), `listRoots` (`rootList`), `renameTo` (`dest`), `getFileInfo` (`fileInfo`)
- `win32-lib/gui/BaseWindow.cpp`: `setClass` (`className`), `createWindow` (`windowName`), `setWindowText` (`text`)
- `win32-lib/gui/ComboBox.cpp`: `getItemText` (`storage`)
- `win32-lib/gui/Control.cpp`: `getText` (`storage`)
- `win32-lib/gui/DibFrameBuffer.cpp`: `blitToDibSection` (`rect`), `blitTransparentToDibSection` (`rect`), `blitFromDibSection` (`rect`), `stretchFromDibSection` (`srcRect`, `dstRect`), `updateDibSection` (`newDim`, `pixelFormat`)
- `win32-lib/gui/ScaledIcons.cpp`: `drawCentered` (`dis`)
- `win32-lib/gui/SpinControl.cpp`: `setAutoAccelerationParams` (`limitters`, `deltas`)
- `win32-lib/gui/drawing/DeviceContext.cpp`: `DeviceContext` (`compatibleDevice`, `pntWnd`)
- `win32-lib/gui/drawing/DeviceContext.h`: `DeviceContext` (`pntWnd`)
- `win32-lib/gui/drawing/Graphics.cpp`: `fillRect` (`brush`)
- `win32-lib/log-writer/FileAccount.cpp`: `getFileName` (`fileName`), `print` (`dt`), `format` (`dt`)
- `win32-lib/log-writer/LogDump.cpp`: `LogEntry` (`dt`), `updateLogHeaderLines` (`dt`), `updateLogDumpLines` (`dt`)
- `win32-lib/network/socket/../socket/SocketAddressIPv4.h`: `toString` (`address`)
- `win32-lib/network/socket/../socket/SocketIPv4.h`: `getAcceptedSocket` (`addr`)
- `win32-lib/util/CommandLineArgs.cpp`: `getArgVector` (`out`)
- `win32-lib/util/DateTime.cpp`: `toString` (`out`)
- `win32-lib/util/ListenerContainer.h`: `removeListener` (`listener`)
- `win32-lib/util/StringStorage.cpp`: `getSubstring` (`substr`), `isEqualTo` (`other`)
- `win32-lib/util/TextEncoding.cpp`: `decodeUtf8` (`result`)
- `win32-lib/win-system/DibSection.cpp`: `DibSection` (`pf`, `dim`), `blitToDibSection` (`rect`), `blitTransparentToDibSection` (`rect`), `blitFromDibSection` (`rect`), `stretchFromDibSection` (`srcRect`, `dstRect`), `setupPaletteBMI` (`pf`, `dim`), `setupBitFieldsBMI` (`bmi`, `pf`, `dim`), `setupBMIHeader` (`header`, `pf`, `dim`), `openDIBSection` (`pf`, `dim`)
- `win32-lib/win-system/Environment.cpp`: `getErrStr` (`out`, `out`), `getCurrentModulePath` (`out`)
- `win32-lib/win-system/RegistryKey.cpp`: `getValueAsString` (`out`), `tryOpenSubKey` (`openedKey`)
- `win32-lib/win-system/Screen.cpp`: `getBMI` (`bmi`), `fillPixelFormat` (`bmi`)
- `win32-lib/win-system/SystemInformation.cpp`: `getDesktopAllArea` (`rc`), `getDesktopArea` (`rc`)
- `win32-lib/win-system/WinClipboard.cpp`: `getString` (`str`), `setString` (`str`), `addCR` (`str`), `removeCR` (`str`)
- `win32-lib/win-system/WinProcessCommandLine.cpp`: `optionParser` (`out`), `findOptionValue` (`strOut`), `getArgument` (`strOut`), `getOption` (`strOut`)
- `win32-lib/win-system/WindowsApplication.cpp`: `processDialogMessage` (`msg`)

Not counted above, from the categories left out of scope:

- `void *` buffers with a size (`InputStream::read`,
  `OutputStream::write`, `DataInputStream::readFully`,
  `DataOutputStream::writeFully`, `WinFile::write`): `std::span` of
  bytes, not references.
- Pointers to integers used as single out-parameters (`getInt`,
  `getBoolean`, `parseInt`, `normalizeScale`, `getBorderSize`,
  `virtualCodeToKeySym`, `windowProc`'s `useDefWindowProc` and so
  on): 22 are borrowed and never null-checked; as above, returning the
  value may be better than a reference. The `StringParser::parse*`
  functions accept a null `out` deliberately (to only validate).

## Caveats

- **Non-null is inferred from absence**: the body never tests the
  pointer, and no caller passes a null literal or has a null default.
  A caller passing a pointer that can be null at run time (a member
  not set yet, say) is not caught. The 44 groups of call sites whose
  arguments are not `&x`, `this` or a passed-on parameter were read;
  they pass members such as `m_input`, `m_output`, `m_frameBuffer`,
  `m_toCopy`, or `unique_ptr::get()`, set before use as far as seen.
- **Passed to the standard library**: a pointer passed into
  `std::make_unique` or a container counts as kept unless checked by
  hand. Checked and treated as borrows: `memset` in
  `registerWindowClass`, and `make_unique<DibSection>` and
  `make_unique<ViewerInstance>`, whose constructors only read the
  pointee.
- **Passed to Win32 APIs**: treated as borrows (the API uses it during
  the call). Not verified per API.
- **Local objects**: an argument to a local variable's constructor
  counts as a borrow; an object that then escapes (moved into a
  container, say) would be missed.
- **Overloads** with the same parameter type at the same position are
  merged, which can only make a parameter look kept or nullable when
  it is not.
- **Virtual families** are grouped by name, position and type, not by
  the real override relationship; two unrelated virtual functions with
  the same name and parameter type would be judged together.
- Only code compiled in Debug x64 is seen.

## Other things found along the way

- clang's `-Wreorder-ctor` warns in nine constructors (`BaseDialog`,
  `ConnectionConfig`, `ViewerWindow`, `ViewerInstance` twice,
  `ScaleManager`, `DesktopWindow`, `ControlTrayIcon`, `KeySymTest`).
  None is a bug: each initialiser uses only constants, parameters, or
  the address of a member declared earlier. MSVC's equivalent, C5038,
  is off by default.
- `LoginDialog::setConConf` is declared but never defined or called:
  dead.

## How to repeat it

The queries were run ad hoc and are not in the tree. The core of it,
for `clang-query -p . -f <file> <source>`:

```
set output dump
set bind-root false
set traversal AsIs
# all pointer parameters
match parmVarDecl(hasType(qualType(hasCanonicalType(pointerType()))), unless(isExpansionInSystemHeader()))
# e.g. kept by a member initialiser (P is the matcher above)
match cxxCtorInitializer(withInitializer(ignoringParenImpCasts(declRefExpr(to(P.bind("ctorinit"))))))
# e.g. passed on
match callExpr(forEachArgumentWithParam(ignoringParenImpCasts(declRefExpr(to(P.bind("src")))), parmVarDecl().bind("dst")))
```

Two pitfalls: under `IgnoreUnlessSpelledInSource` implicit casts are
invisible, so `CK_PointerToBoolean` never matches; and
`cxxDeleteExpr(has(...))` and `arraySubscriptExpr(hasBase(...))`
made `clang-query` 22.1.3 exit silently, ending the rest of the
query file (`hasDescendant` works).
