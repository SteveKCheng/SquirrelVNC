// Copyright (C) 2026 Steve Cheng.
//
//-------------------------------------------------------------------------
// This file is part of SquirrelVNC, a modified version of the TightVNC
// software (https://www.tightvnc.com/).
//
// This program is free software; you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation; either version 2 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License along
// with this program; if not, write to the Free Software Foundation, Inc.,
// 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
//-------------------------------------------------------------------------
//

#include "RfbParsing.h"

#include "rfb/StandardPixelFormatFactory.h"
#include "util/Exception.h"
#include "util/TextEncoding.h"

#include "ViewerBufferSize.h"

#include <cstddef>

// Limits on lengths sent by the server, so that a malicious or broken server
// cannot make the viewer allocate an arbitrary amount of memory.
static const UINT32 MAX_CUT_TEXT_LENGTH = 64 * 1024 * 1024;
static const UINT32 MAX_DESKTOP_NAME_LENGTH = 64 * 1024;

namespace RfbParsing
{

Task<> skipBytes(AwaitableReader &reader, size_t length)
{
  char buffer[4096];
  while (length > 0) {
    size_t chunk = length < sizeof(buffer) ? length : sizeof(buffer);
    co_await reader.readFully(buffer, chunk);
    length -= chunk;
  }
}

Task<PixelFormat> readPixelFormat(AwaitableReader &reader, LogWriter &logWriter)
{
  PixelFormat pixelFormat;
  pixelFormat.bitsPerPixel = co_await reader.readUInt8();
  pixelFormat.colorDepth = co_await reader.readUInt8();
  pixelFormat.bigEndian = !!co_await reader.readUInt8();
  bool isTrueColour = co_await reader.readUInt8() != 0;
  pixelFormat.redMax = co_await reader.readUInt16();
  pixelFormat.greenMax = co_await reader.readUInt16();
  pixelFormat.blueMax = co_await reader.readUInt16();
  pixelFormat.redShift = co_await reader.readUInt8();
  pixelFormat.greenShift = co_await reader.readUInt8();
  pixelFormat.blueShift = co_await reader.readUInt8();
  co_await reader.readUInt8(); // padding bytes (3)
  co_await reader.readUInt8();
  co_await reader.readUInt8();

  const unsigned short bitsPerPixel = pixelFormat.bitsPerPixel;
  if (bitsPerPixel != 8 && bitsPerPixel != 16 && bitsPerPixel != 32) {
    throw Exception(_T("Only 8, 16 or 32 bits per pixel supported!"));
  }
  if (!isTrueColour) {
    // Colour maps are not supported, and the colour fields of a format
    // without true colour mean nothing. The viewer sets a true-colour format
    // of its own before it requests any update, so until then a standard
    // format with the same bits per pixel stands in.
    logWriter.info(_T("The server's pixel format uses a colour map; ")
                   _T("a true-colour format will be requested."));
    co_return StandardPixelFormatFactory::createPixelFormat(bitsPerPixel);
  }
  if (!pixelFormat.isValid()) {
    logWriter.error(_T("Invalid pixel format: bits-per-pixel: %d, depth: %d, ")
                    _T("red-max: %d, green-max: %d, blue-max: %d, ")
                    _T("red-shift: %d, green-shift: %d, blue-shift: %d"),
                    pixelFormat.bitsPerPixel, pixelFormat.colorDepth,
                    pixelFormat.redMax, pixelFormat.greenMax, pixelFormat.blueMax,
                    pixelFormat.redShift, pixelFormat.greenShift, pixelFormat.blueShift);
    throw Exception(_T("The server's pixel format is invalid"));
  }
  co_return pixelFormat;
}

/**
 * Server send:
 * 2           - U16         - framebuffer-width
 * 2           - U16         - framebuffer-height
 * 16          - PixelFormat - server-pixel-format
 * 4           - U32         - name-length
 * name-length - U8 array    - name-string
 */
Task<ServerInit> readServerInit(AwaitableReader &reader, LogWriter &logWriter)
{
  ServerInit serverInit;
  UINT16 width = co_await reader.readUInt16();
  UINT16 height = co_await reader.readUInt16();
  serverInit.dimension = Dimension(width, height);
  serverInit.pixelFormat = co_await readPixelFormat(reader, logWriter);

  UINT32 sizeInBytes = co_await reader.readUInt32();
  if (sizeInBytes > MAX_DESKTOP_NAME_LENGTH) {
    throw Exception(_T("Desktop name is too long"));
  }
  std::vector<std::byte> buffer(sizeInBytes);
  co_await reader.readFully(buffer.data(), sizeInBytes);
  serverInit.desktopName = TextEncoding::fromRfbText(buffer);
  co_return serverInit;
}

void checkFrameBufferProperties(const Dimension &dimension,
                                const PixelFormat &pixelFormat,
                                UINT64 maxPixels,
                                unsigned int rowAlignmentLog2)
{
  const int width = dimension.width;
  const int height = dimension.height;
  if (width < 0 || height < 0 || !pixelFormat.isValid()) {
    throw Exception(_T("Invalid framebuffer properties"));
  }
  if (static_cast<UINT64>(width) * height > maxPixels ||
      !ViewerBufferSize::fitsFrameBuffer(width, height,
                                         pixelFormat.bitsPerPixel / 8,
                                         rowAlignmentLog2)) {
    throw Exception(_T("Framebuffer is too large"));
  }
}

Task<UINT32> readServerMessageType(AwaitableReader &reader)
{
  // Viewer in common case read first byte (UINT8) as message id,
  // but if first byte is equal to 0xFC then it's TightVNC extension message and
  // must read next 3 bytes and create UINT32 message id for processing.

  static const UINT16 SERVER_MSG_SPECIAL_TIGHT_CODE = 0xFC;

  UINT32 msgType = co_await reader.readUInt8();
  if (msgType == SERVER_MSG_SPECIAL_TIGHT_CODE) {
    for (int i = 0; i < 3; i++) {
      msgType <<= 8;
      msgType += co_await reader.readUInt8();
    }
  }
  co_return msgType;
}

Task<UINT16> readFbUpdateHeader(AwaitableReader &reader)
{
  // read padding: one byte
  co_await reader.readUInt8();
  co_return co_await reader.readUInt16();
}

Task<RectangleHeader> readRectangleHeader(AwaitableReader &reader)
{
  RectangleHeader header;
  header.rect.left = co_await reader.readUInt16();
  header.rect.top = co_await reader.readUInt16();
  header.rect.setWidth(co_await reader.readUInt16());
  header.rect.setHeight(co_await reader.readUInt16());
  header.encodingType = co_await reader.readInt32();
  co_return header;
}

void checkRectangleInside(const Dimension &fbDimension, const Rect &rect)
{
  if (!fbDimension.getRect().intersection(rect).isEqualTo(rect)) {
    throw Exception(_T("Error in protocol: incorrect size of rectangle"));
  }
}

Task<RichCursor> readRichCursor(AwaitableReader &reader, const Rect &rect,
                                UINT8 bytesPerPixel)
{
  RichCursor cursor;
  // The width and height were read as 16-bit values.
  cursor.width = static_cast<UINT16>(rect.getWidth());
  cursor.height = static_cast<UINT16>(rect.getHeight());
  cursor.hotSpot = Point(rect.left, rect.top);

  if (cursor.width > ViewerBufferSize::MAX_CURSOR_SIZE ||
      cursor.height > ViewerBufferSize::MAX_CURSOR_SIZE) {
    throw Exception(_T("Rich cursor is too large"));
  }

  size_t pixelsLen = cursor.width * cursor.height * bytesPerPixel;
  if (pixelsLen != 0) {
    cursor.pixels.resize(pixelsLen);
    co_await reader.readFully(&cursor.pixels.front(), pixelsLen);

    size_t bitmaskLen = ((cursor.width + 7) / 8) * cursor.height;
    cursor.bitmask.resize(bitmaskLen);
    co_await reader.readFully(&cursor.bitmask.front(), bitmaskLen);
  }
  co_return cursor;
}

Task<> readSetColorMapEntries(AwaitableReader &reader)
{
  // read padding: 1 byte
  co_await reader.readUInt8();

  co_await reader.readUInt16(); // first-colour
  UINT16 numberOfColours = co_await reader.readUInt16();
  // Red, green and blue, 16 bits each.
  co_await skipBytes(reader, static_cast<size_t>(numberOfColours) * 6);
}

Task<std::optional<StringStorage>> readServerCutText(AwaitableReader &reader,
                                                     LogWriter &logWriter)
{
  // read padding: three byte
  co_await reader.readUInt16();
  co_await reader.readUInt8();

  UINT32 length = co_await reader.readUInt32();
  if (length > MAX_CUT_TEXT_LENGTH) {
    logWriter.warning(_T("Cut text is too long (%u bytes), ignoring it"), length);
    co_await skipBytes(reader, length);
    co_return std::nullopt;
  }
  std::vector<std::byte> buffer(length);
  co_await reader.readFully(buffer.data(), length);
  // Latin-1 by the specification, but some servers send UTF-8.
  co_return TextEncoding::fromRfbText(buffer);
}

Task<std::optional<StringStorage>> readServerCutTextUtf8(AwaitableReader &reader,
                                                         LogWriter &logWriter)
{
  UINT32 length = co_await reader.readUInt32();
  if (length == 0)
    co_return std::nullopt;
  if (length > MAX_CUT_TEXT_LENGTH) {
    logWriter.warning(_T("Cut text is too long (%u bytes), ignoring it"), length);
    co_await skipBytes(reader, length);
    co_return std::nullopt;
  }

  std::vector<std::byte> buffer(length);
  co_await reader.readFully(buffer.data(), length);
  co_return TextEncoding::fromUtf8(buffer);
}

} // namespace RfbParsing
