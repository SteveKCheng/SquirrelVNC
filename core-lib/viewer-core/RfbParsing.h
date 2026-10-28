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

#ifndef _RFB_PARSING_H_
#define _RFB_PARSING_H_

#include "io-lib/AwaitableReader.h"
#include "log-writer/LogWriter.h"
#include "region/Dimension.h"
#include "region/Point.h"
#include "region/Rect.h"
#include "rfb/PixelFormat.h"
#include "util/StringStorage.h"
#include "util/Task.h"
#include "util/inttypes.h"

#include <optional>
#include <vector>

//
// Parsing of the server messages that the viewer reads after the security
// handshake, shared by RemoteViewerCore and rfb-fuzz so that the fuzzer
// tests the code the viewer runs. Only parsing and the checks on what is
// parsed are here; what is done with the result (the frame buffers, the
// decoders, the adapter) is left to the caller. This header must not
// include Asio, so that rfb-fuzz does not depend on it.
//
// Each function that reads (all but the check...() functions) is a
// coroutine (util/Task.h) reading from reader, which
// must outlive the returned task; it may be resumed on any thread, but on
// one thread at a time, as reader requires. Unless said otherwise, each
// throws whatever reading the input throws (IOException), and the input
// is left part-way through the message when it throws.
//
namespace RfbParsing
{
  //
  // Reads and discards length bytes.
  //
  Task<> skipBytes(AwaitableReader &reader, size_t length);

  //
  // Reads a PIXEL_FORMAT.
  //
  // Pre-conditions: the next bytes of the input are a PIXEL_FORMAT.
  // Post-conditions: the PIXEL_FORMAT has been consumed, and the result is
  // valid (PixelFormat::isValid()). A true-colour format is returned as
  // read. A format without true colour is replaced by the standard format
  // with the same bits per pixel, because its colour fields mean nothing;
  // that is logged to logWriter, as is an invalid format.
  // Throws Exception if the bits per pixel are not 8, 16 or 32, or if a
  // true-colour format is not valid.
  //
  Task<PixelFormat> readPixelFormat(AwaitableReader &reader, LogWriter &logWriter);

  //
  // The ServerInit message.
  //
  struct ServerInit
  {
    Dimension dimension;
    PixelFormat pixelFormat;
    StringStorage desktopName;
  };

  //
  // Reads ServerInit.
  //
  // Pre-conditions: the next bytes of the input are ServerInit.
  // Post-conditions: the message has been read whole. The pixel format is
  // as readPixelFormat() returns it; the dimension is not checked (see
  // checkFrameBufferProperties()). The desktop name is decoded by
  // TextEncoding::fromRfbText().
  // Throws Exception as readPixelFormat() does, or if the desktop name is
  // longer than 64 KiB, leaving it unread.
  //
  Task<ServerInit> readServerInit(AwaitableReader &reader, LogWriter &logWriter);

  //
  // Checks the properties of a frame buffer that the server asks for, in
  // ServerInit or in a DesktopSize rectangle.
  //
  // Post-conditions: returns if pixelFormat is valid and the frame buffer,
  // and the viewer's buffers derived from it, fit: at most maxPixels
  // pixels, and ViewerBufferSize::fitsFrameBuffer().
  // Throws Exception otherwise.
  //
  void checkFrameBufferProperties(const Dimension &dimension,
                                  const PixelFormat &pixelFormat,
                                  UINT64 maxPixels);

  //
  // Reads the type of the next server message: one byte, or for TightVNC's
  // extension messages, 0xFC followed by three more bytes, as one 32-bit
  // number (rfb/MsgDefs.h).
  //
  // Pre-conditions: the input is at a message boundary.
  // Post-conditions: the type has been read, and the input is at the body.
  //
  Task<UINT32> readServerMessageType(AwaitableReader &reader);

  //
  // Reads the header of FramebufferUpdate, after its type.
  //
  // Pre-conditions: the message type has been read.
  // Post-conditions: returns the number of rectangles; the input is at the
  // first rectangle.
  //
  Task<UINT16> readFbUpdateHeader(AwaitableReader &reader);

  //
  // The header of a rectangle of FramebufferUpdate.
  //
  struct RectangleHeader
  {
    Rect rect;
    INT32 encodingType;
  };

  //
  // Reads the header of a rectangle.
  //
  // Pre-conditions: the input is at a rectangle of FramebufferUpdate.
  // Post-conditions: the header has been read; the input is at the
  // rectangle's data. The rectangle is not checked (see
  // checkRectangleInside()).
  //
  Task<RectangleHeader> readRectangleHeader(AwaitableReader &reader);

  //
  // Checks that the rectangle of an update, of an encoding that is not a
  // pseudo-encoding, lies inside a frame buffer of fbDimension.
  //
  // Throws Exception if it does not.
  //
  void checkRectangleInside(const Dimension &fbDimension, const Rect &rect);

  //
  // The data of a RichCursor pseudo-rectangle.
  //
  struct RichCursor
  {
    Point hotSpot;
    UINT16 width;
    UINT16 height;
    // width * height pixels of the frame buffer's format; empty if either
    // is 0.
    std::vector<UINT8> pixels;
    // One bit per pixel, rows padded to whole bytes; empty if pixels is.
    std::vector<UINT8> bitmask;
  };

  //
  // Reads the data of a RichCursor pseudo-rectangle.
  //
  // Pre-conditions: rect is the rectangle's header, as read by
  // readRectangleHeader(); bytesPerPixel is that of the frame buffer's
  // format.
  // Post-conditions: the data has been read whole.
  // Throws Exception, before reading, if the cursor is wider or taller
  // than ViewerBufferSize::MAX_CURSOR_SIZE.
  //
  Task<RichCursor> readRichCursor(AwaitableReader &reader, const Rect &rect,
                                  UINT8 bytesPerPixel);

  //
  // Reads SetColourMapEntries and discards it: colour maps are not
  // supported.
  //
  // Pre-conditions: the message type has been read.
  // Post-conditions: the message has been read whole.
  //
  Task<> readSetColorMapEntries(AwaitableReader &reader);

  //
  // Reads ServerCutText.
  //
  // Pre-conditions: the message type has been read.
  // Post-conditions: the message has been read whole. Returns its text,
  // decoded by TextEncoding::fromRfbText(): Latin-1 as the specification
  // says, or UTF-8 if it is valid UTF-8, ending at the first zero byte.
  // Text longer than 64 MiB is skipped, logged to logWriter, and nullopt
  // returned.
  //
  Task<std::optional<StringStorage>> readServerCutText(AwaitableReader &reader,
                                                       LogWriter &logWriter);

  //
  // Reads TightVNC's ServerCutTextUtf8 (code 0xfc000200).
  //
  // Pre-conditions: the message type has been read.
  // Post-conditions: the message has been read whole. Returns its text
  // decoded as UTF-8: invalid UTF-8 becomes U+FFFD, and the text ends at
  // the first zero byte. Returns nullopt for empty text, and for text
  // longer than 64 MiB, which is skipped and logged to logWriter.
  //
  Task<std::optional<StringStorage>> readServerCutTextUtf8(AwaitableReader &reader,
                                                           LogWriter &logWriter);
}

#endif // _RFB_PARSING_H_
