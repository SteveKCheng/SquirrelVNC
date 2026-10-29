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

//
// Unit tests of FrameBuffer's rows: their length with an alignment, and
// that drawing and copying walk them by it, leaving the bytes past each
// row's pixels alone.
//

#include "UnitTest.h"

#include "rfb/FrameBuffer.h"
#include "rfb/StandardPixelFormatFactory.h"

#include <cstring>

namespace
{
  const UINT8 PADDING = 0xA5;

  // Fills the whole buffer, padding included, with PADDING.
  void fillPadding(FrameBuffer &fb)
  {
    memset(fb.getBuffer(), PADDING, fb.getBufferSize());
  }

  // Whether every byte past the pixels of every row is PADDING.
  bool isPaddingKept(const FrameBuffer &fb)
  {
    const size_t pixelBytes = static_cast<size_t>(fb.getDimension().width) * fb.getBytesPerPixel();
    for (int y = 0; y < fb.getDimension().height; y++) {
      const UINT8 *row = static_cast<const UINT8 *>(fb.getBufferPtr(0, y));
      for (size_t i = pixelBytes; i < fb.getBytesPerRow(); i++) {
        if (row[i] != PADDING) {
          return false;
        }
      }
    }
    return true;
  }

  // An 8-bit frame buffer whose pixel at (x, y) is y * 16 + x.
  void fillIndexed(FrameBuffer &fb)
  {
    for (int y = 0; y < fb.getDimension().height; y++) {
      for (int x = 0; x < fb.getDimension().width; x++) {
        *static_cast<UINT8 *>(fb.getBufferPtr(x, y)) = static_cast<UINT8>(y * 16 + x);
      }
    }
  }

  UINT8 at(const FrameBuffer &fb, int x, int y)
  {
    return *static_cast<const UINT8 *>(fb.getBufferPtr(x, y));
  }
}

void runFrameBufferTests()
{
  check(FrameBuffer::bytesPerRowFor(5, 1, 0) == 5 &&
        FrameBuffer::bytesPerRowFor(5, 1, 2) == 8 &&
        FrameBuffer::bytesPerRowFor(8, 1, 2) == 8 &&
        FrameBuffer::bytesPerRowFor(5, 4, 3) == 24 &&
        FrameBuffer::bytesPerRowFor(3, 2, 2) == 8 &&
        FrameBuffer::bytesPerRowFor(0, 4, 3) == 0,
        "FrameBuffer::bytesPerRowFor() rounds rows up to the alignment");

  const PixelFormat pf8 = StandardPixelFormatFactory::create8bppPixelFormat();

  // 5 pixels wide, rows of 8 bytes.
  FrameBuffer fb;
  fb.setRowAlignmentLog2(3);
  fb.setProperties(Dimension(5, 4), pf8);
  check(fb.getBytesPerRow() == 8 && fb.getBufferSize() == 32 &&
        static_cast<UINT8 *>(fb.getBufferPtr(2, 3)) ==
          static_cast<UINT8 *>(fb.getBuffer()) + 3 * 8 + 2,
        "FrameBuffer with aligned rows: row length, size and pixel addresses");

  fillPadding(fb);
  fb.setColor(0, 0, 0);
  fb.fillRect(Rect(1, 1, 5, 3), 0x07);
  bool same = true;
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 5; x++) {
      bool inside = x >= 1 && y >= 1 && y < 3;
      same = same && at(fb, x, y) == (inside ? 0x07 : 0x00);
    }
  }
  check(same && isPaddingKept(fb),
        "FrameBuffer::setColor() and fillRect() fill aligned rows' pixels only");

  // Copied into rows of their own length, and back.
  fillIndexed(fb);
  FrameBuffer packed;
  check(packed.clone(fb) && packed.getBytesPerRow() == 5 && packed.getRowAlignmentLog2() == 0,
        "FrameBuffer::clone() keeps the copy's own row alignment");
  same = true;
  for (int y = 0; y < 4; y++) {
    for (int x = 0; x < 5; x++) {
      same = same && at(packed, x, y) == y * 16 + x;
    }
  }
  check(same, "FrameBuffer::clone() copies from aligned rows into packed ones");

  FrameBuffer aligned;
  aligned.setRowAlignmentLog2(2);
  aligned.setProperties(Dimension(5, 4), pf8);
  fillPadding(aligned);
  check(aligned.copyFrom(Rect(0, 0, 5, 4), packed, 0, 0) && aligned.cmpFrom(Rect(0, 0, 5, 4), fb, 0, 0) &&
        isPaddingKept(aligned),
        "FrameBuffer::copyFrom() and cmpFrom() between rows of different lengths");

  // CopyRect, overlapping, down and right, then back up and left.
  fillPadding(fb);
  fillIndexed(fb);
  fb.move(Rect(1, 1, 5, 4), 0, 0);
  same = at(fb, 1, 1) == 0x00 && at(fb, 2, 2) == 0x11 && at(fb, 4, 3) == 0x23 &&
         at(fb, 0, 3) == 0x30 && at(fb, 4, 0) == 0x04;
  fb.move(Rect(0, 0, 4, 3), 1, 1);
  same = same && at(fb, 0, 0) == 0x00 && at(fb, 1, 1) == 0x11 && at(fb, 3, 2) == 0x23 &&
         at(fb, 4, 0) == 0x04;
  check(same && isPaddingKept(fb), "FrameBuffer::move() up and down within aligned rows");
}
