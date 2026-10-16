# Overlay of vcpkg's built-in x64-windows-static triplet (see
# "overlay-triplets" in vcpkg-configuration.json). Identical to the
# built-in one except that zlib-ng is built in zlib-compat mode, so it
# provides zlib.h and the standard zlib API.
set(VCPKG_TARGET_ARCHITECTURE x64)
set(VCPKG_CRT_LINKAGE static)
set(VCPKG_LIBRARY_LINKAGE static)
set(VCPKG_PROVIDED_FORTRAN ON)

if(PORT STREQUAL "zlib-ng")
    set(ZLIB_COMPAT ON)
endif()
