# Portability: building the protocol core away from Windows

SquirrelVNC is a Windows client, but its protocol core is being separated
from the Windows-specific code so the core can be built and tested away from
Windows. The aim is not a GUI client on another platform; it is easier
development — Valgrind and fuzzing, faster builds on a Linux host, and
testing without an interactive Windows desktop.

The code is in two libraries: `core-lib`, the portable protocol core, and
`win32-lib`, the Windows-specific code. The asynchronous rewrite
(`async-networking.md`) removed most of the blocking Win32 code the core
once relied on. `core-lib` already cross-compiles from Linux with clang-cl
and xwin (`cross-compiling.md`), and runs headless under the replay and fuzz
harnesses (`testing.md`). What is left is to finish untangling `core-lib`
from `win32-lib`, and to settle the one cross-cutting issue, TCHAR.

## What `core-lib` still pulls from `win32-lib`

`core-lib` has 185 source files. They include just eight `win32-lib`
headers (resolved along the shared include path `.;core-lib;win32-lib`):

| `win32-lib` header | `core-lib` includers | What is not yet portable |
|---|---|---|
| `util/StringStorage.h` | 10 | TCHAR, `_tcs*`, `_vstprintf_s`; the header still includes `winhdr.h`, so `windows.h`, WinSock, `commctrl.h` and `shellapi.h` reach every file that touches a string. |
| `log-writer/LogWriter.h` (`Logger.h`) | 10 | TCHAR; SAL's `_Printf_format_string_`; includes `util/CharDefs.h`, which pulls `<tchar.h>` under `_WIN32`. |
| `util/TextEncoding.h` | 8 | The header is portable; the `.cpp` converts UTF-8 with `MultiByteToWideChar`/`WideCharToMultiByte` and `static_assert`s that `TCHAR` is UTF-16. |
| `file-lib/File.h` | 4 | Win32 filesystem API (`CreateFile`, `FindFirstFile`, `MoveFile`, …); file transfer only. |
| `file-lib/WinFile.h` | 2 | Win32 file I/O (`CreateFile`/`ReadFile`/`WriteFile`); the sole open-file class; file transfer only. |
| `network/socket/SocketException.h` | 2 | The header is portable (it carries a `std::error_code`); the `.cpp` formats the message with `FormatMessage`. |
| `file-lib/EOFException.h` | 1 | Portable in itself (its base `io-lib/IOException` is in `core-lib`); lives with `file-lib`. |
| `network/HostResolver.h` | 1 | The header is portable Asio; the `.cpp` is `GetAddrInfoExW` (cancellable, UTF-16 names, Punycode). |

Most of the weight — `StringStorage`, `LogWriter`, `TextEncoding` — is the
TCHAR / `StringStorage` cross-cut (see "TCHAR" below). `File`, `WinFile` and
`EOFException` are the file-transfer dependency. `SocketException` and
`HostResolver` are thin Win32 implementations behind portable interfaces: a
non-Windows build needs a second implementation or an `#ifdef`, but their
headers can move.

## Win32 use left inside `core-lib`

Apart from TCHAR, two `core-lib` files still use Windows directly:

- **`rfb/HostPath.cpp`** — `<tchar.h>` and `_tcs*`, the Windows macros
  `IS_HIGH_SURROGATE`/`IS_LOW_SURROGATE` (simple range checks to inline), and
  it relies on `GetAddrInfoW()` to convert internationalised host names to
  Punycode. This is the most Windows-entangled file in the core.
- **`ft-client-lib/UploadOperation.cpp`** — a single `_tcscmp`; a TCHAR-ism,
  not a Win32 call.

No other `core-lib` file includes a Windows system header, and none uses
`windows.h`, WinSock, a Win32 handle type, or a Win32 call directly.

## The file classes

`win32-lib/file-lib` now holds three classes: `File` (filesystem operations —
create, rename, delete, timestamps, mkdir), `WinFile` (an open file, the sole
file-I/O class), and `EOFException`. `core-lib` reaches them only through
file transfer: `viewer-core/FileTransferCapability` pulls in `ft-client-lib`,
which uses all three.

The plan is to rewrite `WinFile` on standard C++ I/O (`<cstdio>` or
`std::basic_filebuf`) and either port `File` to `std::filesystem` or make
file transfer optional in `viewer-core`, so a non-Windows `core-lib` can
leave out `ft-client-lib`, `ft-common` and `file-lib` altogether. File
transfer is not otherwise in use, so the build switch may be the better
trade.

## TCHAR

This is the one cross-cutting issue. 81 of `core-lib`'s 185 files use TCHAR
or `StringStorage`, with 431 `_T(` sites; six format strings use the
Microsoft-only `%hs`, and the rest use `%s`.

Decision (settled): TCHAR stays in `core-lib`, fixed per platform — `WCHAR`
on Windows, as now, and `char` holding UTF-8 on Linux, where a central header
does `typedef char TCHAR` (with `_T()` and the `_tcs*` names to match). On
Linux `%s` then keeps its meaning; the six `%hs` sites need a macro or a
rewrite. Mapping TCHAR to `wchar_t` on Linux is rejected: `%s` in a wide
`printf` would silently change meaning, and Linux's 4-byte `wchar_t` brings
its own differences. UTF-8 `char` in the core on both platforms, converting
at the Win32 boundary, is cleaner still but far larger, and not needed for
the goals here.

## Remaining work, roughly in order

1. **Trim `StringStorage.h`'s `winhdr.h` include** to what `StringStorage`
   actually needs (`<tchar.h>` and the string headers), on Windows. No
   behaviour change, but it exposes which files were relying on `windows.h`
   arriving transitively through a string header.
2. **Add a TCHAR compatibility header for the Linux build** (`char`/UTF-8,
   `_T`, `_tcs*` shims), and a second `TextEncoding` implementation for a
   non-UTF-16 `TCHAR` (its `static_assert` marks the spot). After this, the
   TCHAR-only group — `StringStorage`, `LogWriter`, `TextEncoding` — can move
   into `core-lib`.
3. **Rewrite `WinFile` on standard C++ I/O**, and port `File` or make file
   transfer optional in `viewer-core`.
4. **`HostPath.cpp`:** inline the surrogate macros, and decide how to handle
   the `GetAddrInfoW` Punycode behaviour portably.
5. **The thin network implementations:** give `SocketException.cpp` an
   `#ifdef _WIN32` message format (`FormatMessageW` on Windows; the
   `std::error_code`'s own `message()` elsewhere — it already carries the
   code), and give `HostResolver` an `asio::ip::tcp::resolver` implementation
   for non-Windows.

## Build system

The MSBuild / Visual Studio build stays authoritative. A CMake build already
cross-compiles `core-lib` from Linux (`cross-compiling.md`); a full migration
to CMake is future work. Its two unsolved pieces are `svg2ico` (it renders
icons with Direct2D at build time and cannot run on Linux — the options are a
portable renderer such as lunasvg or resvg, or icons prebuilt on Windows and
fetched as artifacts; see `branding-and-icons.md`) and WinDlgGen, a .NET tool
without MSBuild's easy .NET integration (`dialog-resources.md`).

---

*This note is a consolidated snapshot. The earlier, step-by-step record of
how the core was separated from Windows is in this file's Git history.*
