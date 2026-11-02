# Cross-compiling from Linux with clang-cl and xwin

A CMake build cross-compiles `core-lib` from Linux to Windows x64, as a step
toward a faster iteration workflow than the Windows VM (`caitsith`). This is
in addition to, not a replacement for, the MSBuild/Visual Studio build, which
remains authoritative.

## Scope

  - `core-lib` only. `tvnviewer` needs two build-time tools that do not yet
    cross-build: `svg2ico` (Direct2D/WIC rendering, to be replaced by
    `lunasvg`) and `WinDlgGen` (a `dotnet` tool needing custom build rules).
  - Only the equivalent of MSBuild's `Release|x64`. `core-lib` is a static
    library, so the build compiles and archives it but links nothing; there
    is nothing to run, and (as of 2026-11-02) no testing on `caitsith` was
    done for the cross build.

## Toolchain

LLVM's `clang-cl`, `lld-link`, `llvm-lib`, `llvm-rc` (all in `PATH`) target
`x86_64-pc-windows-msvc`. The Windows SDK and MSVC CRT headers and import
libraries are "splatted" by [xwin](https://github.com/Jake-Shadle/xwin) into:

  - `/opt/xwin/sdk` — the Windows SDK, passed as clang-cl's `/winsdkdir`
    (single version 10.0.26100, auto-detected).
  - `/opt/xwin/crt` — the MSVC CRT, passed as clang-cl's `/vctoolsdir`.

The splat has **no debug CRT and no DIA SDK**, so only the Release, `/MT`
configuration is possible. That is why the vcpkg triplet below is
release-only and why the CMake build uses the static runtime.

The toolchain file is `cmake/xwin-clang-cl.toolchain.cmake`. It is the single
source of truth for "how to target Windows": the project build chain-loads it
through vcpkg, and the vcpkg triplet references it again for the dependencies.
The xwin locations default to `/opt/xwin/{sdk,crt}` and can be overridden with
`XWIN_SDK_DIR` / `XWIN_CRT_DIR`.

## Dependencies with vcpkg

The deps (`asio`, `libjpeg-turbo`, `zlib-ng`, `simpleini` from `vcpkg.json`)
are cross-built by vcpkg. Getting this to work needed:

  - **A vcpkg registry.** Fedora's `vcpkg` package is only the tool; it needs
    `VCPKG_ROOT` pointing at a clone of `github.com/microsoft/vcpkg`. The
    Fedora default is `~/.local/share/vcpkg`:
    `git clone https://github.com/microsoft/vcpkg.git ~/.local/share/vcpkg`
    (a blobless clone, `--filter=blob:none`, is enough and much smaller).
  - **A cross triplet**, `cmake/cross-triplets/x64-windows-static.cmake`,
    passed as an overlay (`VCPKG_OVERLAY_TRIPLETS`) so it takes precedence
    over the repo's `triplets/x64-windows-static.cmake`. It adds, to that
    triplet's static linkage and zlib-ng zlib-compat mode:
      - `VCPKG_CMAKE_SYSTEM_NAME Windows` and `VCPKG_CHAINLOAD_TOOLCHAIN_FILE`
        pointing at the xwin toolchain, so the ports cross-compile;
      - `VCPKG_BUILD_TYPE release`, since there is no debug CRT. Without this
        vcpkg also builds the `-dbg` variant with `-MTd` and the link fails
        for want of `libcmtd`.
  - **Network.** The sandbox reaches `github.com` / `codeload.github.com` but
    not the vcpkg asset CDN (`download.vcpkg.io`) nor SourceForge/GitLab.
    vcpkg falls back from the blocked CDN to the upstream GitHub source URLs,
    which is enough for these four ports. `VCPKG_FORCE_SYSTEM_BINARIES=1`
    avoids vcpkg trying to download its own CMake/Ninja (it uses the system
    ones); `VCPKG_DISABLE_METRICS=1` is set by the Fedora package.

## Building

    export VCPKG_ROOT="$HOME/.local/share/vcpkg"
    cmake -G Ninja -B build/cross -S . \
      -DCMAKE_TOOLCHAIN_FILE="$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake" \
      -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE="$PWD/cmake/xwin-clang-cl.toolchain.cmake" \
      -DVCPKG_TARGET_TRIPLET=x64-windows-static \
      -DVCPKG_OVERLAY_TRIPLETS="$PWD/cmake/cross-triplets" \
      -DCMAKE_BUILD_TYPE=Release
    cmake --build build/cross

with `VCPKG_FORCE_SYSTEM_BINARIES=1` and `VCPKG_DISABLE_METRICS=1` in the
environment. The configure step cross-builds the dependencies (the slow part,
cached afterwards) and the build step produces `build/cross/core-lib.lib`, an
archive of Windows x64 COFF objects.

## Case-sensitivity of system includes

Linux's filesystem is case-sensitive; the xwin splat provides each header
under its real casing plus an all-lowercase symlink, and the UCRT headers are
all lowercase. A few source includes named a header with a casing that
matched neither, which failed only when cross-compiling. They were corrected
to lowercase (fine for MSVC too, being case-insensitive): `<Tchar.h>` ->
`<tchar.h>` in `util/CharDefs.h`, `<ShellAPI.h>` -> `<shellapi.h>` in
`util/winhdr.h`, `<Windowsx.h>` -> `<windowsx.h>` in `gui/ComboBox.cpp`.
`<CommCtrl.h>` was left as is: the SDK does ship that exact casing.

## Known gaps

  - At `/W4`, clang-cl reports warnings MSVC does not (`-Wsign-compare`,
    `-Wreorder-ctor`, `-Wignored-qualifiers`). They are warnings only; the
    native MSVC build stays clean. Not addressed here.
  - Whole-program optimization (`/GL`) and the Release PDB from the vcxproj
    are not reproduced; neither affects whether the code compiles.
  - The cross output has not been run or tested anywhere.
