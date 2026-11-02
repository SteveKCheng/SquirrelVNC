# Portability: Linux builds, a headless RFB client, CMake

## Updates since this note was first written

**Status (2026-10-21).** This note describes the code as inherited from
TightVNC. The plan has since moved on: networking is to be rewritten as
asynchronous (`async-networking.md`), which removes much of the Win32
use described here, so its recommendations are provisional. Once that
rewrite is done, most of this note becomes historical, like
`old-server.md`; it stays as a record of where SquirrelVNC started.

**Status (2026-10-22).** The libraries have been consolidated to 
`core-lib` and `win32-lib`, and each of TightVNC's former libraries
are now just sub-directories of `core-lib` or `win32-lib`.
No Win32-specific code has been ported, yet.

**Status (2026-10-31).** The asynchronous rewrite is done, and with it
much of what this note describes is gone: the `thread` folder,
`WindowsEvent`, `FbUpdateNotifier`, the blocking socket classes,
`WsaStartup`, the stream gates and `LocalMutex`. `core-lib` no longer
includes anything from `win-system`, `gui`, `config-lib` or
`client-config-lib`. The current state, what can move from `win32-lib`
to `core-lib`, and what Win32 use is left in `core-lib`, are in
"Current state (2026-10-31)", next. The sections after it are the
original investigation (2026-10-20), kept as a record, with corrections
inline.

**Status (2026-10-31, later; branch `portability-2`).** TCHAR stays in
`core-lib`, fixed per platform; see "TCHAR: decision". The first
clean-ups are done: `_ASSERT` is `assert` in `core-lib` (and `<crtdbg.h>`
is included nowhere); `DesCrypt`, `Exception`, `Inflater`, `inttypes.h`,
`ListenerContainer.h`, `ZLibBase` and `ZLibException` moved from
`win32-lib/util` to `core-lib/util`; `RfbKeySym` and
`RfbKeySymListener.h` moved from `core-lib/rfb` to `win32-lib/rfb`.
"Current state" below is corrected inline.

## Current state (2026-10-31)

### What `core-lib` depends on

`core-lib` has 177 source files. Its includes from `win32-lib` reach only
these folders:

| `win32-lib` header | Included by (`core-lib` files) | Windows-specific? |
|---|---|---|
| `util/inttypes.h` | 20 | no |
| `util/Exception.h` | 12 | TCHAR only |
| `log-writer/LogWriter.h` | 11 | TCHAR, and SAL's `_Printf_format_string_` |
| `util/TextEncoding.h` | 8 | header: TCHAR; `.cpp`: `MultiByteToWideChar` |
| `util/StringStorage.h` | 6 | TCHAR, `_vstprintf_s`, `_ASSERT`; includes `winhdr.h` |
| `util/CommonHeader.h` | 4 | pulls in `winhdr.h` and `StringTable` (below) |
| `file-lib/File.h`, `WinFileChannel.h`, `EOFException.h` | 5, for file transfer | `File`, `WinFile`: Win32 file API |
| `util/Inflater.h`, `ZLibException.h` | 3, 2 | TCHAR, `_ASSERT` |
| `util/DateTime.h` | 2 (file transfer) | `FILETIME`, `SYSTEMTIME` |
| `util/ListenerContainer.h` | 2 | no |
| `network/socket/SocketException.h` | 2 | `.cpp`: `FormatMessage`, `WSAGetLastError` |
| `network/HostResolver.h`, `TcpKeepAlive.h` | 1 each | `.cpp`: `GetAddrInfoExW`; `SOCKET`, `DWORD`, `BOOL` |
| `util/DesCrypt.h` | 1 | no |
| `util/Keymap.h` | 1 (`rfb/RfbKeySym.h`) | Windows keyboard (`VK_*`) |

*Since moved into `core-lib`: `inttypes.h`, `Exception`, `Inflater`,
`ZLibException`, `ListenerContainer.h` and `DesCrypt`. `Keymap.h` is no
longer included from `core-lib`, since `RfbKeySym` moved to `win32-lib`.
`DateTime.h` is no longer included from `core-lib` either, since file
transfer stopped using it (it throttles with `std::chrono::steady_clock`).*

The direction of the old plan's step 3 ("the core libraries never
include `win-system`, `gui` or `config-lib`") has thus been reached,
except for keyboard translation, and the file-transfer code's use of
`file-lib`. What is left is mostly TCHAR, plus a few thin Win32
implementations behind portable interfaces.

### Files in `win32-lib` that are not Windows-specific

Grouped by what stops each from building on another platform. "Users"
are the files that include it, outside its own folder.

**Portable now: no Win32 and no TCHAR.**

| File | Users | Notes |
|---|---|---|
| `util/inttypes.h` | most of `core-lib` | `UINT8` … `INT64` as typedefs of standard types; could become `<cstdint>` aliases. |
| `util/ListenerContainer.h` | `ft-client-lib` | Header only, `<vector>`. |
| `util/DesCrypt.{h,cpp}` | `viewer-core/VncAuthentication.cpp`, `util/VncPassCrypt.cpp` | The `.cpp` includes `util/CommonHeader.h` but uses nothing from it, only `memset` (needs `<cstring>` instead). |
| `util/ZLibBase.{h,cpp}` | `util/Inflater.h` | zlib only. |
| `util/VncPassCrypt.{h,cpp}` | `tvnviewer/ConnectionData.cpp` | Same unneeded `CommonHeader.h` include as `DesCrypt.cpp`. Only the GUI uses it (passwords in `.vnc` files and the registry), so it can move or stay. |
| `util/Unicode.{h,cpp}` | `log-writer/FileAccount.cpp` | Portable, but its only purpose is the UTF-16 BOM of the log file; stays with `log-writer`. |

~~So `inttypes.h`, `ListenerContainer.h`, `DesCrypt` and `ZLibBase` can move
to `core-lib` as they are (after dropping the stray include).~~ *Moved,
and `DesCrypt.cpp` includes `<cstring>` instead.*

**Portable except for TCHAR and `StringStorage`.** These move once the
TCHAR question is settled (see "TCHAR" below), or with `StringStorage`
itself, since it is what they need from Windows. *(Settled: TCHAR is
allowed in `core-lib`; see "TCHAR: decision".)*

- ~~`util/Exception.{h,cpp}`, `util/ZLibException.{h,cpp}`,~~
  `file-lib/EOFException.{h,cpp}` (whose base, `io-lib/IOException`, is
  in `core-lib` already). *`Exception` and `ZLibException` moved.*
- ~~`util/Inflater.{h,cpp}`: also two `_ASSERT`s.~~ *Moved, with
  `assert`.*
- `log-writer/LogWriter.{h,cpp}`, `log-writer/Logger.h`: also SAL's
  `_Printf_format_string_` (an empty macro elsewhere would do), and
  `util/CharDefs.h`, which only includes `<tchar.h>` under `_WIN32`.
- `util/StringStorage.{h,cpp}`: `_tcs*`, `_vstprintf_s`, `_ASSERT`; its
  header includes `winhdr.h`, which brings `windows.h`, WinSock,
  `commctrl.h` and `ShellAPI.h` into every file that uses a string.
  Trimming that include to `<tchar.h>` (plus what `StringStorage` really
  needs) is worth doing first, on Windows: it shows which files rely on
  `windows.h` arriving that way.
- `util/TextEncoding.h`: the interface is portable; the `.cpp` converts
  UTF-8 with `MultiByteToWideChar`/`WideCharToMultiByte`, and
  `static_assert`s that `TCHAR` is UTF-16. A second implementation is
  needed only with a non-UTF-16 `TCHAR`.
- `network/socket/SocketException.h`: the header is portable; the
  `.cpp` formats messages with `FormatMessage`.
- `util/CommandLineArgs.{h,cpp}`, `util/CommandLineFormatException`,
  `util/CommandLineFormatHelp`, `util/StringParser`: portable apart from
  TCHAR, but only the GUI uses them; no reason to move them.

**Portable interfaces over thin Win32 implementations.** `core-lib`
needs these, so a non-Windows build needs a second implementation (or
an `#ifdef`), but the headers can move, or the code can be rewritten
portably:

- `network/TcpKeepAlive.{h,cpp}`: `setsockopt` with option names that
  Windows and Linux share; only the types (`SOCKET`, `DWORD`, `BOOL`)
  and `SOCKET_ERROR` differ. Taking Asio's
  `native_handle_type` and `int` would make it portable as it is.
- `network/HostResolver.{h,cpp}`: the interface is portable Asio; the
  implementation is `GetAddrInfoExW` by choice (cancellable, UTF-16
  names). Elsewhere, `asio::ip::tcp::resolver` behind the same
  `asyncResolve()` would do for tests.
- `network/socket/SocketException.cpp`: `std::system_category().message()`
  would be portable, but it returns a narrow string, whose encoding
  under MSVC would need checking; `FormatMessageW` is safer on Windows.
- `util/DateTime.{h,cpp}`: `FILETIME`-based. The GUI and `file-lib` use
  it for file times. ~~`ft-client-lib` uses it only to throttle progress
  reports to one per 300 ms (`DownloadOperation`, `UploadOperation`),
  which `std::chrono::steady_clock` does better.~~ *(Done 2026-11-01:
  file transfer throttles with `steady_clock` directly; `DateTime` stays
  in `win32-lib` for the GUI and file times.)*
- `file-lib/*`: `File` and `WinFile` call `CreateFile`,
  `FindFirstFile`, `MoveFile` and so on, for file transfer only.
  `std::filesystem` and `<cstdio>` cover it, as before.

**Windows by nature, staying in `win32-lib`:** `win-system`, `gui`,
`config-lib` (registry; `IniFileSettingsManager` also reads with
`CreateFile`), `client-config-lib`, `util/ResourceLoader`,
`util/StringTable`, `util/Keymap` (which should go with `RfbKeySym`; see
below), `util/CommonHeader.h`, `util/winhdr.h`, and the rest of
`log-writer` (`FileAccount`, `FileLogger`, `LogDump`: thread and process
ids, `SYSTEMTIME`, UTF-16 log files).

~~**Dead:** `util/GetCPUtime.{h,cpp}` is compiled but included nowhere.
(Its header ends with `#endif _GETCPUTIME_H_`, a stray token MSVC only
warns about.)~~ *Deleted (2026-11-01; `dead-code.md`).*

### Win32-specific code left in `core-lib`

Besides TCHAR (below), from a scan of every file for Win32 headers,
types and calls:

1. **Keyboard translation: `rfb/RfbKeySym.{h,cpp}`.** Turns Windows
   virtual keys into keysyms, with `ToUnicodeEx`, `VK_*`, `WCHAR` and
   `std::wstring`, and includes `util/Keymap.h` from `win32-lib`. Only
   `tvnviewer/DesktopWindow` and `harnesses/viewer-keysym-test` use it;
   `viewer-core` does not. ~~Move it to `tvnviewer` (with `Keymap`, and
   `RfbKeySymListener.h`, which is portable but only meaningful there).~~
   This is the largest piece, and the only one that is Windows by nature.
   *Moved to `win32-lib/rfb` instead, with `RfbKeySymListener.h`, next
   to `util/Keymap`: the keysym test harness uses it too.*
2. ~~**`region/Rect.h`: conversions to and from Win32 `RECT`**:~~ a
   constructor from `RECT`, `toWindowsRect()`, `fromWindowsRect()`. For
   them it includes `util/CommonHeader.h`, so every user of `Rect` gets
   `windows.h`. About a dozen call sites in `tvnviewer` and `win32-lib`;
   free functions in `win32-lib` (for instance next to the GDI helpers)
   would take their place. *Done (2026-11-01): `toWindowsRect(const
   Rect &)` and `fromWindowsRect(const RECT &)` in
   `win32-lib/region/WindowsRect.h`, and `Rect.h` includes only
   `<vector>`. The files that had `windows.h` or `inttypes.h` only
   through it now include what they use (`FrameBuffer.h`,
   `DecoderStore.h`, `JpegDecompressor.h`, `tvnviewer/ScaleManager.h`).*
3. **`util/CommonHeader.h` as a catch-all include**, in ~~`region/Rect.h`~~,
   `rfb/RfbKeySym.cpp`, `viewer-core/CapabilitiesManager.h` and
   `viewer-core/RemoteViewerCore.h`. It brings `winhdr.h` and
   `StringTable` (Win32 string resources). Each of these needs only
   `StringStorage`, or nothing. *(`RfbKeySym.cpp` is no longer in
   `core-lib`, and `Rect.h` no longer includes it.)*
4. ~~**`_ASSERT` from `<crtdbg.h>`**, instead of `assert`:~~ *Done:
   each is `assert`, but `Decoder.cpp`'s, which is deleted. Unknown
   encodings do reach it, since the encoding comes from the server, and
   `processPseudoEncoding()` rejects them with an exception.*
   `ft-client-lib/DownloadOperation.cpp` (`_ASSERT(FALSE)`),
   `ft-client-lib/UploadOperation.cpp` (two), `rfb/FrameBuffer.cpp`,
   `rfb/StandardPixelFormatFactory.cpp`, `rfb/RfbKeySym.cpp` and
   `viewer-core/Decoder.cpp`. `ft-client-lib/FileTransferReplyBuffer.cpp`
   includes `<crtdbg.h>` without using it. `Decoder.cpp`'s is
   `_ASSERT(TRUE)` under "if the encoding type is unknown, then generate
   assertions", so it never fires: either make it `assert(false)` or
   delete it, after checking that unknown encodings really cannot reach
   it.
5. **`rfb/HostPath.cpp`**: `<tchar.h>`, `_tcs*`, and the Windows macros
   `IS_HIGH_SURROGATE`/`IS_LOW_SURROGATE` (simple range checks to write
   out). Its rules for internationalised names also rely on
   `GetAddrInfoW()` converting them to Punycode.
6. **`viewer-core/RemoteViewerCoreImpl.h`**: the keepalive constants are
   `DWORD`, to match `enableTcpKeepAlive()`; they would follow that
   function's types.

Not Win32, but MSVC-specific: the secure-CRT `_s` functions appear in
`win32-lib` (`_vstprintf_s` in `StringStorage`) and in the tests
(`sprintf_s`), but not in `core-lib`'s own code.

Fine as they are: `TRUE`/`FALSE` in `region/` come from the X11 region
code's own definitions, and in `JpegDecompressor.cpp` from libjpeg.

### TCHAR today

65 of `core-lib`'s 177 files use TCHAR or `StringStorage` (29 in
`ft-client-lib`, 31 in `viewer-core`, 9 in `io-lib`, 5 in `ft-common`, 3
in `rfb`), with 455 `_T(` sites. 64 format strings use `%s` and 6 use
`%hs`; with `TCHAR = char` on Linux, `%s` keeps its meaning, but `%hs` is
a Microsoft extension, not standard C, so those six need a macro or a
rewrite. ~~The recommendation below (UTF-8 `char` as TCHAR off Windows)
still stands.~~ *Decided; see next.*

### TCHAR: decision

*(2026-10-31.)* TCHAR is allowed in `core-lib`: changing it would cause
too much churn. The choice is fixed per platform: `TCHAR` is `WCHAR` on
Windows, as now, and `char` (holding UTF-8) on Linux, where a central
header does `typedef char TCHAR` (with `_T()` and the `_tcs*` names to
match).

UTF-8 strings on Windows too, converted to UTF-16 at each Win32 call, is
possible but not wanted: the GUI calls Windows APIs often enough that
converting on demand only adds complexity, and the strings are short,
so the extra storage of UTF-16 does not matter.

### Suggested order now

1. Small clean-ups on Windows, with no behaviour change: drop the stray
   `CommonHeader.h` includes; ~~move the `RECT` conversions out of `Rect`;~~
   ~~`_ASSERT` to `assert`;~~ ~~`DateTime` to `steady_clock` in file transfer;~~
   ~~delete `GetCPUtime`~~. *(`_ASSERT`, `GetCPUtime`, `RECT` and
   `DateTime` done.)*
2. ~~Move `RfbKeySym` and `Keymap` to `tvnviewer`.~~ *Done: `RfbKeySym`
   moved to `win32-lib`, where `Keymap` already is.*
3. ~~Move the files that are already portable (`inttypes.h`,
   `ListenerContainer.h`, `DesCrypt`, `ZLibBase`) to `core-lib`.~~ *Done,
   with `Exception`, `ZLibException` and `Inflater`.*
4. Trim `StringStorage.h`'s include of `winhdr.h`, then ~~decide on TCHAR,~~
   *(decided: see "TCHAR: decision")* after which the second group above
   can move.
5. Make file transfer optional in `viewer-core`, or port `file-lib`:
   still the open question from the original plan. Until then, a
   non-Windows build of `core-lib` leaves out `ft-client-lib` and
   `ft-common`.

## Introduction

Investigation (2026-10-20) of how far the code can be built away from
Windows. The aim is not a GUI client on another platform, but easier
development:

- tools that run better or only on Linux: Valgrind, fuzzing;
- building on a Linux host, which is faster than the Windows sandbox
  (filesystem performance especially), possibly cross-compiling with
  the LLVM toolchain;
- testing without an interactive Windows desktop, which the sandbox
  does not have.

Summary: the protocol core is already mostly separate from the GUI,
without being designed that way, and a headless test program is quite
feasible. The main work is tidying the boundary between layers. Clang
already compiles the whole tree (after the fixes listed under "Clang").

## Is portable code separated from Windows code?

Not by design, but the dependencies split cleanly along library lines.
From the includes between libraries and the Win32 calls in each:

| Group | Libraries | State |
|---|---|---|
| Already pure | `region`, `io-lib` | only TCHAR and two Unicode conversion calls |
| Nearly pure | `rfb`, `viewer-core`, `ft-common`, `ft-client-lib` | Win32 use is only TCHAR and `_T()` |
| Thin Win32 foundations | `thread`, `network`, `util`, `log-writer`, `file-lib` | about 12 files call Win32 directly (below) |
| Windows by nature | `win-system`, `gui`, `config-lib` (registry), `client-config-lib`, `tvnviewer` | stay on Windows |

The core reaches the Windows-only layer in exactly three places:

- ~~`viewer-core/FbUpdateNotifier.h` uses `win-system/WindowsEvent`;~~
  *(both gone 2026-10-29)*
- ~~`network/socket/*` uses `win-system/WsaStartup`;~~ *(gone in Phase 4
  step 7 of `async-networking.md`: Asio initializes WinSock)*
- `file-lib/WinFile.cpp` uses `SystemException` and `Environment`.

The Win32 calls in the foundations all have standard C++ or POSIX
replacements:

- ~~**Threads.** `LocalMutex` is a `CRITICAL_SECTION`, which is
  recursive, so it maps to `std::recursive_mutex`. `Thread` is
  `CreateThread` plus `WaitForSingleObject`; `suspend()` is only used to
  start the thread after construction, and `setPriority()` and
  `getThreadId()` are never called outside the class. `WindowsEvent`
  maps to a `std::condition_variable`. Rewrite these on the standard
  library for all platforms, with no `#ifdef`.~~ *Done: the `thread`
  folder and `WindowsEvent` are deleted (2026-10-29); the code uses
  `std::thread`, `std::mutex` and `std::condition_variable`.*
- ~~**Sockets.** Plain BSD calls (`socket`, `connect`, `select`, `recv`,
  `send`, `getaddrinfo`). The only differences are `closesocket`,
  `ioctlsocket(FIONREAD)`, `WSAGetLastError`, `SOCKET`, `WSAStartup` and
  `FormatMessage`; a small `#ifdef` layer covers them.~~ *Replaced by
  Asio (`async-networking.md`); what is left is `TcpKeepAlive`,
  `HostResolver` and `SocketException` (see "Current state").*
- **`util`.**
  - `DateTime` is built on `FILETIME`/`SYSTEMTIME`. *(File transfer no
    longer uses it — it throttles with `std::chrono::steady_clock` as of
    2026-11-01 — but the GUI and `file-lib` still need it for file
    times, so it stays in `win32-lib`.)*
  - `TextEncoding` (which replaced `AnsiStringStorage`,
    `UnicodeStringStorage` and `Utf8StringStorage`; see
    `string-encoding.md`) converts UTF-8 with
    `MultiByteToWideChar`/`WideCharToMultiByte`; Latin-1 needs no Win32
    call.
  - ~~`GetCPUtime` uses `GetProcessTimes`.~~ *(Deleted as dead code,
    2026-11-01.)*
  - `ResourceLoader`/`StringTable` read Win32 resources, but only
    `tvnviewer` uses them.
- **Files.** `file-lib` uses `CreateFile`, `FindFirstFile`, `MoveFile`,
  `SetFileTime` and so on; `std::filesystem` plus `<cstdio>` covers it.
  `log-writer/FileAccount` and `FileLogger` also call
  `GetCurrentThreadId`. `LogWriter` itself needs neither.
- **Misplaced code.** `rfb/RfbKeySym` and `util/Keymap` translate Windows
  keystrokes (`ToUnicodeEx`, `VK_*`). They belong in `win-system` or
  `tvnviewer`; `viewer-core` does not use them. *(Still so, 2026-10-31.)*

### TCHAR

This is the one cross-cutting issue. The core libraries have 477 `_T(`
sites and 271 uses of `StringStorage`, and 64 of the format strings use
`%s`. In MSVC's wide printf, `%s` means a wide string; in glibc's
`swprintf` it means a narrow `char *`. So mapping TCHAR to `wchar_t` on
Linux would silently break formatting, and Linux's 4-byte `wchar_t`
brings its own differences.

Recommendation: the Linux build uses `TCHAR = char`, holding UTF-8.

- `_T(x)` becomes `x`, `_tcs*` becomes `str*`, and
  `StringStorage::format()` uses `vsnprintf`.
- `%s` then means the same as on Windows.
- The string classes with `#ifndef _UNICODE` branches are gone; only
  the UTF-8 functions of `TextEncoding` need a second implementation (a
  validating copy), which its `static_assert` on `TCHAR` points to.

The long-term clean version would be UTF-8 `std::string` in the core on
both platforms, converting at the Win32 boundary. That is much bigger
and not needed for the goals here. *(Not to be done; see "TCHAR:
decision".)*

## A command-line RFB client over stdin/stdout

*Done in another form: `harnesses/rfb-replay` (`testing.md`) runs the
core from bytes in memory or over a loopback socket. The gates, the
core's own threads and `FbUpdateNotifier` described below are gone.*

Feasible, and the hook already exists. Besides host/port and an
existing socket, `RemoteViewerCore` can start from a pair of gates:

```cpp
start(RfbInputGate *input, RfbOutputGate *output, CoreEventsAdapter *adapter, ...)
```

The gates wrap any `InputStream`/`OutputStream`, which are two-method
interfaces (`read`/`available` and `write`/`flush`). The program would
need:

- **Streams:** one stream class each over fd 0 and fd 1. On Windows,
  set stdout to binary with `_setmode(_O_BINARY)`.
- **A `CoreEventsAdapter`:** logs events to stderr, and can dump the
  framebuffer to PPM or print a hash per update for regression tests.
- **A way to connect it:**
  - live: `socat TCP:host:5900 EXEC:./rfbcli`, or wayvnc over an `ssh`
    pipe;
  - replay: a recorded server stream piped in, which gives
    deterministic Valgrind runs.

Caveats:

- `RemoteViewerCore` runs two threads of its own: the protocol loop and
  `FbUpdateNotifier`, which calls the adapter.
- `stop()` works by closing the socket, which will not reliably unblock
  a `read()` on stdin. Ending at EOF, or calling `_exit()`, is fine for a
  test program.
- File transfer comes along unconditionally:
  `viewer-core/FileTransferCapability` includes four `ft-client-lib`
  headers, so `ft-client-lib` and `file-lib` must link. Either port
  `file-lib` to `std::filesystem`, or add a build switch that leaves file
  transfer out of `viewer-core`. Since file transfer is not in use so
  far, the switch may be the better trade.

### Fuzzing

The current plan is in `testing.md`.

*Done on Windows: `harnesses/rfb-fuzz` and `harnesses/ini-fuzz`, in the
`ReleaseFuzz` configuration (`testing.md`). The paragraphs below are the
original reasoning.*

Test below `RemoteViewerCore`. Its threads and blocking reads make it a
poor libFuzzer target.
`DecoderOfRectangle::process(RfbInputGate *, FrameBuffer *, FrameBuffer *, const Rect *, LocalMutex *, FbUpdateNotifier *)`
is ideal: a `ByteArrayInputStream` gives the input, a fixed-size
framebuffer receives the result, and nothing runs on another thread. It
needs a stub notifier, because `notify()` dereferences it
unconditionally.

One target per decoder (Tight, ZRLE, Hextile, RRE, RichCursor and so on)
would cover most of the code that handles untrusted input. The
handshake and authentication could get a second target later.

## Clang

Every `.cpp`/`.c` file (202) was parsed with the Clang 22.1.3 front end
in Visual Studio's `clang-tidy`, in clang-cl mode with `/std:c++20`.
(The MSBuild ClangCL toolset is not installed on the sandbox, but
`clang-tidy.exe` is; see "Checking with Clang" below.) Only five root
causes appeared, all now fixed on the `portability` branch:

1. `util/ListenerContainer.h`: `vector<T>::iterator` needed `typename`.
   That one header caused about 90% of the errors. (`af0df3a`)
2. `register` in `util/DesCrypt.h`, removed in C++17. (`364f44e`)
3. `std::auto_ptr` in `tvnviewer/DesktopWindow.h`, removed in C++17. It
   compiled only because the MSVC projects default to C++14. Now
   `std::unique_ptr`, created with `reset(new ...)` because
   `make_unique()` cannot convert `this` to the protected base
   `RfbKeySymListener`. (`a360a3f`)
4. `min()` with mixed `int`/`size_t` in `tvnviewer/ConnectionData.cpp`,
   which compiled only through the `<windows.h>` macro. (`6224eb8`)
5. A real bug: `tvnviewer/FileRenameDialog.cpp` had
   `NewFolderDialog::NewFolderDialog(parent);` in the constructor body.
   MSVC treated it as building and discarding a temporary, so the base
   class was default-constructed and `parent` was ignored: the File
   Transfer rename dialog opened without an owner window. (`37e1051`)

After these, all 202 files parse without errors. The MSVC projects
now compile as C++20 too (`LanguageStandard` `stdcpp20` in every
project), which needed no source changes.

### Checking with Clang

The check copies the tracked files (`git ls-files`) to a scratch folder
and runs, for each source file, from an environment set up by
`vcvars64.bat`:

```
clang-tidy --quiet "--checks=-*,cert-dcl58-cpp" FILE -- --driver-mode=cl
    /nologo /std:c++20 /EHsc /MT /DWIN32 /DNDEBUG /D_UNICODE /DUNICODE
    /DNOMINMAX /D_LIB /W3 -Wno-microsoft -I<root> -I<file's dir>
    -I<repo>\vcpkg_installed\x64-windows-static\x64-windows-static\include
```

clang-tidy needs at least one check enabled, or it exits without
parsing; `cert-dcl58-cpp` is a cheap one that never fires here. The
compiler errors are reported as `clang-diagnostic-error`.

### Strict aliasing

Scan (2026-10-22) for code that breaks C++'s type-aliasing rules: an
object read or written through an lvalue of another type, other than a
character type or the signed/unsigned variant of its own type. MSVC
never optimizes on these rules, and neither does clang-cl by default
(it implies `-fno-strict-aliasing`, as MSVC does). GCC and Clang on
Linux do from `-O2`, so this matters for a Linux build of `core-lib`
for fuzzing or Valgrind. Until it is fixed, such a build can use
`-fno-strict-aliasing`.

Fixed: `win-system/VersionInfo.cpp` read the halves of a `DWORD`
through `unsigned short *`. (`fb322ec`)

Remaining, on pixel buffers in `core-lib/rfb`:

- `FrameBuffer::overlayT<PIXEL_T>()`: both buffers cast to `PIXEL_T *`.

The buffers are `new UINT8[]` (`FrameBuffer::resizeBuffer()`) or DIB
section memory (`DibFrameBuffer`). In C++20 an `unsigned char` array
implicitly creates objects such as `UINT32` in its storage as needed
(P0593), so a buffer accessed consistently as one pixel type is then
defined. A buffer always is: changing the pixel format reallocates it,
and decoders write pixels with `memcpy`, which is always allowed.
Before C++20 the casts are undefined in any case, and alignment is
only right because pixel offsets are multiples of the pixel size.

The robust fix is a pair of helpers, `loadPixel<T>(const void *)` and
`storePixel<T>(void *, T)`, built on `memcpy` of `sizeof(T)` bytes.
Compilers turn these into single loads and stores, so the pixel loops
lose no speed, and alignment stops mattering.

`rfb/PixelFormat.cpp`'s `initBigEndianByNative()` tested the byte order
by reading the inactive member of a union, which C++ (unlike C) does
not define. It now uses C++20's `std::endian::native`.

Checked and not a problem: pointers from `LPARAM`, `WPARAM`,
`GWLP_USERDATA`, `GlobalLock()` and `LockResource()`, which point to
objects of the type they are cast to; structures passed to Win32 as
`LPBITMAPINFO` and the like, which our code does not access through
the cast; `RegistrySettingsManager` reading a `UINT` through `int *`
(a signed/unsigned variant); and accesses through `char *` or
`UINT8 *`.

## Build system

**CMake is worth it, but not first.** The build quirks to carry over:

- vcpkg manifest mode with overlay triplets;
- static CRT;
- `.rc` resources with a UTF-8 code page;
- the embedded manifest;
- the custom targets for `svg2ico` (`RenderIcons`, `RenderToolbarIcons`)
- the custom targets for WinDlgGen

`svg2ico` is the one problem for cross-compiling: it renders with
Direct2D at build time and cannot run on Linux. The options are Wine
(Direct2D support there is doubtful), a second host-only renderer such
as lunasvg or resvg for cross builds, or icons built on Windows and
fetched as an artifact. That reopens the Direct2D decision in
`branding-and-icons.md`.

[WinDlgGen](dialog-resources.md) is a .NET program, which may introduce 
complications for CMake as it does not have MSBuild's easy integration 
with .NET projects.

**Cross-compiling from Linux:** clang-cl + lld-link + llvm-rc, with
xwin to fetch the MSVC CRT and Windows SDK, and a CMake toolchain file.
It keeps the MSVC ABI, the static CRT and the vcpkg `*-windows-static`
triplets, so the exe matches the MSBuild one. llvm-mingw also works,
but brings a different CRT and headers, and more behaviour differences.

## Recommendations, in order

**Provisional; the final plan will differ.** In particular, rewriting
`thread` on the standard library and the socket compatibility header
(step 3) would be thrown away by the asynchronous rewrite, which replaces
both with Asio; see `async-networking.md`.

1. *(Done.)* **Fuzz on Windows first, at almost no cost.** MSVC supports
   AddressSanitizer (`/fsanitize=address`) and libFuzzer
   (`/fsanitize=fuzzer`) natively, and neither needs a desktop. The
   decoder fuzz targets above could run on the sandbox with no porting
   at all. Valgrind is the only tool here that really needs Linux, and
   ASan catches most of what it would.
2. **The cheap Clang fixes.** Done; see "Clang" above.
3. **Untangle the layers** so that the core libraries never include
   `win-system`, `gui` or `config-lib`:
   - ~~rewrite `thread` and `WindowsEvent` on the standard library;~~
     *(done 2026-10-29)*
   - ~~move `RfbKeySym`/`Keymap` out of the core;~~ *(done 2026-10-31)*
   - ~~add a small socket compatibility header;~~ *(Asio instead)*
   - make file transfer optional in `viewer-core`, or port `file-lib`;
   - add a TCHAR compatibility header for the Linux build.
4. **Add CMake alongside MSBuild**, at first only for the portable
   libraries plus the stdin/stdout program and fuzz targets. That gives
   native Linux builds for Valgrind, libFuzzer and quick iteration
   without touching the shipped build.
5. **Move the full app to CMake and cross-compile with clang-cl** only
   if step 4 works well, once the `svg2ico` question is settled. Moving
   everything at once would put risk on the build that is deployed.

Steps 3 and 4 are roughly a few dozen files of mechanical work. The
TCHAR compatibility header and the thread rewrite are the only parts
that need real care.
