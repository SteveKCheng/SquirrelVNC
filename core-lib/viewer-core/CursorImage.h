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

#ifndef _CURSOR_IMAGE_H_
#define _CURSOR_IMAGE_H_

#include "region/Point.h"
#include "rfb/PixelFormat.h"
#include "util/inttypes.h"

#include <vector>

namespace RfbParsing
{
  struct RichCursor;
}

//
// A cursor shape from the server, in a form any GUI can show: the
// application draws it as its own pointer (a real Windows cursor), rather
// than the core drawing it into the frame buffer, so the frame buffer
// holds only what the server shows.
//
// A shape of no pixels (width or height 0) means that the server shows no
// cursor.
//
struct CursorImage
{
  // The point of the shape at the pointer's position, from the top left of
  // the shape. From the server: it may lie outside the shape.
  Point hotSpot;
  int width = 0;
  int height = 0;
  // width * height pixels, row by row from the top, each 0xAARRGGBB (so B,
  // G, R, A in memory on a little-endian machine), with 8 bits per
  // component. Alpha is 0 (and the colour black) where the pixel is
  // transparent, else 255.
  std::vector<UINT32> pixels;

  //
  // The shape of a RichCursor pseudo-rectangle.
  //
  // Pre-conditions: cursor was read by RfbParsing::readRichCursor() for
  // pixelFormat's size of pixel, so that pixels and bitmask hold the whole
  // shape; pixelFormat is valid (PixelFormat::isValid()).
  // Post-conditions: the result has the size and hot spot of cursor; each
  // pixel's components are scaled from pixelFormat's maxima to 255, rounded
  // to nearest, and it is opaque where its bit in the bitmask is set.
  //
  static CursorImage fromRichCursor(const RfbParsing::RichCursor &cursor,
                                    const PixelFormat &pixelFormat);
};

#endif // _CURSOR_IMAGE_H_
