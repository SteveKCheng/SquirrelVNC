# vcpkg triplet for cross-building the dependencies from Linux with clang-cl
# and the xwin SDK. Passed as an overlay triplet (VCPKG_OVERLAY_TRIPLETS), so
# it takes precedence over the repo's triplets/x64-windows-static.cmake; the
# static linkage and zlib-ng zlib-compat mode match that triplet, and the two
# extra lines point vcpkg at the cross toolchain.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)

set(VCPKG_CMAKE_SYSTEM_NAME Windows)
set(VCPKG_CHAINLOAD_TOOLCHAIN_FILE "${CMAKE_CURRENT_LIST_DIR}/../xwin-clang-cl.toolchain.cmake")

# The xwin splat has no debug CRT (and no DIA), so build only the release
# variant -- which is also the only configuration we cross-compile anyway.
set(VCPKG_BUILD_TYPE release)

if(PORT STREQUAL "zlib-ng")
    set(ZLIB_COMPAT ON)
endif()
