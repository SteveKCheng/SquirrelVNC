// Copyright (C) 2011,2012 GlavSoft LLC.
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

#ifndef __SCREEN_H__
#define __SCREEN_H__

#include "util/CommonHeader.h"
#include "rfb/PixelFormat.h"
#include "region/Rect.h"
#include "region/Dimension.h"

// This class get info for a windows desktop.
class Screen
{
public:
  Screen();
  ~Screen();

  void update();

  // Returns a PixelFormat that was at latest call of the
  // update() function.
  PixelFormat getPixelFormat();

  // Returns a rectangle that was at latest call of the
  // update() function.
  // The rectangle is a rectangle of windows virtual desktop including
  // all monitors (coordinates can be negative).
  Rect getDesktopRect();

  // This structure can be used by user code.
  struct BMI
  {
    BITMAPINFOHEADER bmiHeader;
    UINT32 red;
    UINT32 green;
    UINT32 blue;
  };

  struct Palette8bitBMI
  {
    BITMAPINFOHEADER bmiHeader;
    RGBQUAD rgbQuad[256];
  };

  // Fills *bmi with the format of the screen, from the bitmap of the
  // screen DC (GetDC(0)), which covers the whole virtual desktop. With
  // several monitors this assumes they share one format, as they do on
  // current Windows.
  // Post-conditions: bmi->bmiHeader describes the screen bitmap; if its
  // biCompression is BI_BITFIELDS, red, green and blue hold the colour
  // masks; the screen DC has been released.
  // Throws Exception if the DC or the format cannot be obtained.
  void getBMI(BMI *bmi);

private:
  void fillPixelFormat(const BMI *bmi);
  // Returns the position of the lowest set bit in bits, from 0 to 31,
  // or 0 if no bit is set, so that the result is always a valid shift
  // count for a UINT32.
  static inline int findFirstBit(const UINT32 bits);

  void fillScreenRect();

  PixelFormat m_pixelFormat;
  Rect m_virtDesktopRect;
};

#endif // __SCREEN_H__
