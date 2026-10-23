// Copyright (C) 2008,2009,2010,2011,2012 GlavSoft LLC.
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

#ifndef __PIXELFORMAT_H__
#define __PIXELFORMAT_H__

struct PixelFormat
{
  PixelFormat();
  // Initializes the bigEndian field by native machine value.
  // Post-conditions: bigEndian is true if and only if the machine is big
  // endian; no other field changes.
  void initBigEndianByNative();

  bool isEqualTo(const PixelFormat *pf) const;

  // Returns true if this is a true-colour format the viewer can use:
  // - bitsPerPixel is 8, 16 or 32, and colorDepth is from 1 to bitsPerPixel;
  // - each colour maximum is 2^n - 1 for some n >= 1, and its shift plus n
  //   is at most bitsPerPixel, so each colour mask lies within the pixel;
  // - the three colour masks do not overlap.
  // Code that converts colours divides by the maxima and shifts by the
  // shifts, so every format must pass this check before it is used.
  // Formats from the network are checked where they are read.
  bool isValid() const;

  unsigned short bitsPerPixel;
  unsigned short colorDepth;

  unsigned short redMax;
  unsigned short greenMax;
  unsigned short blueMax;

  unsigned short redShift;
  unsigned short greenShift;
  unsigned short blueShift;

  bool bigEndian;
};

#endif // __PIXELFORMAT_H__
