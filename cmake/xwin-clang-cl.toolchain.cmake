# Cross-compilation toolchain: Linux host -> Windows x64, using clang-cl with
# the Windows SDK and MSVC CRT headers/import-libraries "splatted" by xwin
# (https://github.com/Jake-Shadle/xwin) into /opt/xwin.
#
# The splat has no debug CRT and no DIA SDK, so only the Release, /MT
# configuration is supported -- which is exactly MSBuild's Release|x64.
#
# Used in two places, so there is one source of truth for "how to target
# Windows": as the project toolchain (chain-loaded by vcpkg.cmake via
# VCPKG_CHAINLOAD_TOOLCHAIN_FILE) and, for the dependencies, referenced again
# from the cross vcpkg triplet.

set(CMAKE_SYSTEM_NAME Windows)
set(CMAKE_SYSTEM_PROCESSOR AMD64)

# Locations of the splatted SDK/CRT; override with -D or the environment.
if(DEFINED ENV{XWIN_SDK_DIR})
  set(XWIN_SDK_DIR "$ENV{XWIN_SDK_DIR}" CACHE PATH "xwin Windows SDK root (/winsdkdir)")
else()
  set(XWIN_SDK_DIR "/opt/xwin/sdk" CACHE PATH "xwin Windows SDK root (/winsdkdir)")
endif()
if(DEFINED ENV{XWIN_CRT_DIR})
  set(XWIN_CRT_DIR "$ENV{XWIN_CRT_DIR}" CACHE PATH "xwin MSVC CRT root (/vctoolsdir)")
else()
  set(XWIN_CRT_DIR "/opt/xwin/crt" CACHE PATH "xwin MSVC CRT root (/vctoolsdir)")
endif()

# The LLVM tools that stand in for the MSVC ones.
set(CMAKE_C_COMPILER   clang-cl)
set(CMAKE_CXX_COMPILER clang-cl)
set(CMAKE_RC_COMPILER  llvm-rc)
set(CMAKE_AR           llvm-lib)
set(CMAKE_MT           llvm-mt)
# clang-cl drives the link; make it use lld-link rather than looking for
# Microsoft's link.exe.
set(CMAKE_LINKER       lld-link)

# clang-cl here already defaults to the Windows-MSVC triple, but be explicit.
# /winsdkdir and /vctoolsdir point clang-cl at the splatted headers; the
# SDK's single version (10.0.26100) is auto-detected under them.
set(_xwin_cl "--target=x86_64-pc-windows-msvc /winsdkdir ${XWIN_SDK_DIR} /vctoolsdir ${XWIN_CRT_DIR}")
set(CMAKE_C_FLAGS_INIT   "${_xwin_cl}")
set(CMAKE_CXX_FLAGS_INIT "${_xwin_cl}")

# Where the import libraries live, for whatever lld-link does run (DLLs/exes
# among the dependencies; a static library like core-lib only gets archived).
set(_xwin_libpaths "/libpath:${XWIN_CRT_DIR}/lib/x64 /libpath:${XWIN_SDK_DIR}/lib/10.0.26100/ucrt/x64 /libpath:${XWIN_SDK_DIR}/lib/10.0.26100/um/x64")
set(CMAKE_EXE_LINKER_FLAGS_INIT    "${_xwin_libpaths}")
set(CMAKE_SHARED_LINKER_FLAGS_INIT "${_xwin_libpaths}")
set(CMAKE_MODULE_LINKER_FLAGS_INIT "${_xwin_libpaths}")

# The target cannot run here, and a full link needs more than a bare compile,
# so let CMake's compiler probe stop at a static library.
set(CMAKE_TRY_COMPILE_TARGET_TYPE STATIC_LIBRARY)

# Run tools from the host; find headers and libraries in the target sysroots.
set(CMAKE_FIND_ROOT_PATH "${XWIN_SDK_DIR}" "${XWIN_CRT_DIR}")
set(CMAKE_FIND_ROOT_PATH_MODE_PROGRAM NEVER)
set(CMAKE_FIND_ROOT_PATH_MODE_LIBRARY BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_INCLUDE BOTH)
set(CMAKE_FIND_ROOT_PATH_MODE_PACKAGE BOTH)
