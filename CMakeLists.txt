# CMake build for core-lib, used to cross-compile the portable protocol core
# from Linux to Windows x64 with clang-cl and the xwin SDK. See
# cmake/xwin-clang-cl.toolchain.cmake and notes/cross-compiling.md.
#
# Scope: core-lib only, and only the equivalent of MSBuild's Release|x64.
# tvnviewer is out of scope here because it needs two build-time tools
# (svg2ico and WinDlgGen) that do not yet cross-build. core-lib is a static
# library, so this compiles and archives it but links nothing.
#
# Configure with the vcpkg toolchain, chain-loading the xwin toolchain, e.g.:
#   cmake -G Ninja -B build/cross \
#     -DCMAKE_TOOLCHAIN_FILE=$VCPKG_ROOT/scripts/buildsystems/vcpkg.cmake \
#     -DVCPKG_CHAINLOAD_TOOLCHAIN_FILE=$PWD/cmake/xwin-clang-cl.toolchain.cmake \
#     -DVCPKG_TARGET_TRIPLET=x64-windows-static \
#     -DVCPKG_OVERLAY_TRIPLETS=$PWD/cmake/cross-triplets \
#     -DCMAKE_BUILD_TYPE=Release

cmake_minimum_required(VERSION 3.21)

# Select the CRT by the MSVC runtime abstraction; the libraries use the static
# runtime (/MT), as in MSBuild's Release configuration.
cmake_policy(SET CMP0091 NEW)

project(SquirrelVNC CXX C)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_MSVC_RUNTIME_LIBRARY "MultiThreaded")

if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
  set(CMAKE_BUILD_TYPE Release CACHE STRING "" FORCE)
endif()

# Dependencies from vcpkg (manifest mode, vcpkg.json). asio is header-only;
# zlib (zlib-ng in zlib-compat mode) and libjpeg-turbo are static libraries.
find_package(ZLIB REQUIRED)
find_package(JPEG REQUIRED)
find_package(asio CONFIG REQUIRED)

add_library(core-lib STATIC
  core-lib/util/DesCrypt.cpp
  core-lib/util/Exception.cpp
  core-lib/util/Inflater.cpp
  core-lib/util/ZLibBase.cpp
  core-lib/util/ZLibException.cpp
  core-lib/region/Region.cpp
  core-lib/region/x11region.c
  core-lib/io-lib/AwaitableReader.cpp
  core-lib/io-lib/ByteArrayInputStream.cpp
  core-lib/io-lib/DataInputStream.cpp
  core-lib/io-lib/DataOutputStream.cpp
  core-lib/io-lib/InputStream.cpp
  core-lib/io-lib/IOException.cpp
  core-lib/io-lib/OutputStream.cpp
  core-lib/rfb/AuthDefs.cpp
  core-lib/rfb/EncodingDefs.cpp
  core-lib/rfb/FrameBuffer.cpp
  core-lib/rfb/HostPath.cpp
  core-lib/rfb/MsgDefs.cpp
  core-lib/rfb/PixelFormat.cpp
  core-lib/rfb/StandardPixelFormatFactory.cpp
  core-lib/rfb/TunnelDefs.cpp
  core-lib/rfb/VendorDefs.cpp
  core-lib/viewer-core/AuthHandler.cpp
  core-lib/viewer-core/CapabilitiesManager.cpp
  core-lib/viewer-core/CapsContainer.cpp
  core-lib/viewer-core/CompressionLevel.cpp
  core-lib/viewer-core/CopyRectDecoder.cpp
  core-lib/viewer-core/CursorImage.cpp
  core-lib/viewer-core/Decoder.cpp
  core-lib/viewer-core/DecoderOfRectangle.cpp
  core-lib/viewer-core/DecoderStore.cpp
  core-lib/viewer-core/BlockingWorkPool.cpp
  core-lib/viewer-core/NetworkThread.cpp
  core-lib/viewer-core/TcpAcceptor.cpp
  core-lib/viewer-core/QueuedOutputStream.cpp
  core-lib/viewer-core/DesktopSizeDecoder.cpp
  core-lib/viewer-core/FileTransferCapability.cpp
  core-lib/viewer-core/HexTileDecoder.cpp
  core-lib/viewer-core/JpegDecompressor.cpp
  core-lib/viewer-core/JpegQualityLevel.cpp
  core-lib/viewer-core/LastRectDecoder.cpp
  core-lib/viewer-core/PointerPosDecoder.cpp
  core-lib/viewer-core/PseudoDecoder.cpp
  core-lib/viewer-core/RawDecoder.cpp
  core-lib/viewer-core/RemoteViewerCore.cpp
  core-lib/viewer-core/RemoteViewerCoreImpl.cpp
  core-lib/viewer-core/RfbClientToServerMessage.cpp
  core-lib/viewer-core/RfbCutTextEventClientMessage.cpp
  core-lib/viewer-core/RfbFramebufferUpdateRequestClientMessage.cpp
  core-lib/viewer-core/RfbParsing.cpp
  core-lib/viewer-core/RfbKeyEventClientMessage.cpp
  core-lib/viewer-core/RfbPointerEventClientMessage.cpp
  core-lib/viewer-core/RfbSetEncodingsClientMessage.cpp
  core-lib/viewer-core/RfbSetPixelFormatClientMessage.cpp
  core-lib/viewer-core/RichCursorDecoder.cpp
  core-lib/viewer-core/RreDecoder.cpp
  core-lib/viewer-core/TightDecoder.cpp
  core-lib/viewer-core/VncAuthentication.cpp
  core-lib/viewer-core/VncAuthenticationHandler.cpp
  core-lib/viewer-core/ZrleDecoder.cpp
  core-lib/ft-common/FileInfo.cpp
  core-lib/ft-common/FolderListener.cpp
  core-lib/ft-common/FTMessage.cpp
  core-lib/ft-common/OperationNotSupportedException.cpp
  core-lib/ft-client-lib/CopyOperation.cpp
  core-lib/ft-client-lib/DownloadOperation.cpp
  core-lib/ft-client-lib/FileInfoList.cpp
  core-lib/ft-client-lib/FileTransferCore.cpp
  core-lib/ft-client-lib/FileTransferEventAdapter.cpp
  core-lib/ft-client-lib/FileTransferEventHandler.cpp
  core-lib/ft-client-lib/FileTransferInterface.cpp
  core-lib/ft-client-lib/FileTransferMessageProcessor.cpp
  core-lib/ft-client-lib/FileTransferOperation.cpp
  core-lib/ft-client-lib/FileTransferReplyBuffer.cpp
  core-lib/ft-client-lib/FileTransferRequestSender.cpp
  core-lib/ft-client-lib/LocalFilesDeleteOperation.cpp
  core-lib/ft-client-lib/OperationNotPermittedException.cpp
  core-lib/ft-client-lib/OperationSupport.cpp
  core-lib/ft-client-lib/RemoteFileListOperation.cpp
  core-lib/ft-client-lib/RemoteFileRenameOperation.cpp
  core-lib/ft-client-lib/RemoteFilesDeleteOperation.cpp
  core-lib/ft-client-lib/RemoteFolderCreateOperation.cpp
  core-lib/ft-client-lib/UploadOperation.cpp
)

# Code includes headers by subdirectory ("util/...", "region/...") and from
# both libraries, so core-lib and win32-lib are on the include path, plus the
# repo root (the vcxproj's "..;..\core-lib;..\win32-lib").
target_include_directories(core-lib PUBLIC
  ${CMAKE_CURRENT_SOURCE_DIR}
  ${CMAKE_CURRENT_SOURCE_DIR}/core-lib
  ${CMAKE_CURRENT_SOURCE_DIR}/win32-lib
)

# Matches Directory.Build.props and core-lib.vcxproj: Windows 10 target (so
# Asio selects the same features everywhere), no min/max macros, the Unicode
# character set. NDEBUG comes from the Release build type.
target_compile_definitions(core-lib PRIVATE
  WINVER=0x0A00
  _WIN32_WINNT=0x0A00
  NOMINMAX
  UNICODE
  _UNICODE
)

# /W4 and /permissive- (ConformanceMode), as in the vcxproj. C++20 comes from
# CMAKE_CXX_STANDARD. Not reproduced: whole-program optimization (/GL) and the
# Release PDB, neither of which affects whether the code compiles.
target_compile_options(core-lib PRIVATE /W4 /permissive-)

target_link_libraries(core-lib PRIVATE ZLIB::ZLIB JPEG::JPEG asio::asio)
