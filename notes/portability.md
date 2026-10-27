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

- `viewer-core/FbUpdateNotifier.h` uses `win-system/WindowsEvent`;
- ~~`network/socket/*` uses `win-system/WsaStartup`;~~ *(gone in Phase 4
  step 7 of `async-networking.md`: Asio initializes WinSock)*
- `file-lib/WinFile.cpp` uses `SystemException` and `Environment`.

The Win32 calls in the foundations all have standard C++ or POSIX
replacements:

- **Threads.** `LocalMutex` is a `CRITICAL_SECTION`, which is
  recursive, so it maps to `std::recursive_mutex`. `Thread` is
  `CreateThread` plus `WaitForSingleObject`; `suspend()` is only used to
  start the thread after construction, and `setPriority()` and
  `getThreadId()` are never called outside the class. `WindowsEvent`
  maps to a `std::condition_variable`. Rewrite these on the standard
  library for all platforms, with no `#ifdef`.
- **Sockets.** Plain BSD calls (`socket`, `connect`, `select`, `recv`,
  `send`, `getaddrinfo`). The only differences are `closesocket`,
  `ioctlsocket(FIONREAD)`, `WSAGetLastError`, `SOCKET`, `WSAStartup` and
  `FormatMessage`; a small `#ifdef` layer covers them.
- **`util`.**
  - `DateTime` is built on `FILETIME`/`SYSTEMTIME`; it could move to
    `std::chrono`.
  - `TextEncoding` (which replaced `AnsiStringStorage`,
    `UnicodeStringStorage` and `Utf8StringStorage`; see
    `string-encoding.md`) converts UTF-8 with
    `MultiByteToWideChar`/`WideCharToMultiByte`; Latin-1 needs no Win32
    call.
  - `GetCPUtime` uses `GetProcessTimes`.
  - `ResourceLoader`/`StringTable` read Win32 resources, but only
    `tvnviewer` uses them.
- **Files.** `file-lib` uses `CreateFile`, `FindFirstFile`, `MoveFile`,
  `SetFileTime` and so on; `std::filesystem` plus `<cstdio>` covers it.
  `log-writer/FileAccount` and `FileLogger` also call
  `GetCurrentThreadId`. `LogWriter` itself needs neither.
- **Misplaced code.** `rfb/RfbKeySym` and `util/Keymap` translate Windows
  keystrokes (`ToUnicodeEx`, `VK_*`). They belong in `win-system` or
  `tvnviewer`; `viewer-core` does not use them.

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
and not needed for the goals here.

## A command-line RFB client over stdin/stdout

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

1. **Fuzz on Windows first, at almost no cost.** MSVC supports
   AddressSanitizer (`/fsanitize=address`) and libFuzzer
   (`/fsanitize=fuzzer`) natively, and neither needs a desktop. The
   decoder fuzz targets above could run on the sandbox with no porting
   at all. Valgrind is the only tool here that really needs Linux, and
   ASan catches most of what it would.
2. **The cheap Clang fixes.** Done; see "Clang" above.
3. **Untangle the layers** so that the core libraries never include
   `win-system`, `gui` or `config-lib`:
   - rewrite `thread` and `WindowsEvent` on the standard library;
   - move `RfbKeySym`/`Keymap` out of the core;
   - add a small socket compatibility header;
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
