// Copyright (C) 2009,2010,2011,2012 GlavSoft LLC.
// All rights reserved.
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

#include "PixelFormat.h"

#include <bit>
#include <string.h>

PixelFormat::PixelFormat()
{
  memset(this, 0, sizeof(PixelFormat));
}

void PixelFormat::initBigEndianByNative()
{
  bigEndian = (std::endian::native == std::endian::big);
}

// Returns the mask of a colour component with the given maximum and shift
// in a pixel of bitsPerPixel bits, or 0 if the maximum is not 2^n - 1 for
// some n >= 1 or the component does not fit in the pixel.
// Pre-conditions: bitsPerPixel <= 32.
static unsigned int getColorMask(unsigned int max, unsigned int shift,
                                 unsigned int bitsPerPixel)
{
  if (max == 0 || (max & (max + 1)) != 0) {
    return 0;
  }
  unsigned int width = 0;
  while ((max >> width) != 0) {
    width++;
  }
  if (shift + width > bitsPerPixel) {
    return 0;
  }
  return max << shift;
}

bool PixelFormat::isValid() const
{
  if (bitsPerPixel != 8 && bitsPerPixel != 16 && bitsPerPixel != 32) {
    return false;
  }
  if (colorDepth == 0 || colorDepth > bitsPerPixel) {
    return false;
  }
  unsigned int redMask = getColorMask(redMax, redShift, bitsPerPixel);
  unsigned int greenMask = getColorMask(greenMax, greenShift, bitsPerPixel);
  unsigned int blueMask = getColorMask(blueMax, blueShift, bitsPerPixel);
  return redMask != 0 && greenMask != 0 && blueMask != 0 &&
         (redMask & greenMask) == 0 && (redMask & blueMask) == 0 &&
         (greenMask & blueMask) == 0;
}

bool PixelFormat::isEqualTo(const PixelFormat &pf) const {
  return bitsPerPixel == pf.bitsPerPixel &&
         colorDepth   == pf.colorDepth &&
         redMax       == pf.redMax &&
         greenMax     == pf.greenMax &&
         blueMax      == pf.blueMax &&
         redShift     == pf.redShift &&
         greenShift   == pf.greenShift &&
         blueShift    == pf.blueShift &&
         bigEndian    == pf.bigEndian;
}
