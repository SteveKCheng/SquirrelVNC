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

#include "Screen.h"
#include "util/Exception.h"
#include "util/ScopeExit.h"
#include "win-system/Environment.h"

Screen::Screen()
{
  update();
}

Screen::~Screen()
{
}

void Screen::update()
{
  BMI bmi;
  getBMI(&bmi);

  fillPixelFormat(&bmi);
  fillScreenRect();
}

PixelFormat Screen::getPixelFormat()
{
  return m_pixelFormat;
}

Rect Screen::getDesktopRect()
{
  return m_virtDesktopRect;
}

void Screen::getBMI(BMI *bmi)
{
  HDC screenDC = GetDC(nullptr);
  if (screenDC == nullptr) {
    throw Exception(_T("Can't get a screen DC"));
  }
  ScopeExit releaseDC([&] { ReleaseDC(nullptr, screenDC); });
  // The bitmap belongs to the DC, so it is not deleted.
  HBITMAP hbm = (HBITMAP)GetCurrentObject(screenDC, OBJ_BITMAP);

  *bmi = {};
  bmi->bmiHeader.biSize = sizeof(BITMAPINFOHEADER);

  // With biBitCount == 0, GetDIBits() fills only the header. Called again
  // for BI_BITFIELDS, it fills the three colour masks after it; for other
  // formats it would write a colour table, which BMI has no room for.
  bool isOk = GetDIBits(screenDC, hbm, 0, 0, nullptr, (LPBITMAPINFO)bmi, DIB_RGB_COLORS) != 0;
  if (isOk && bmi->bmiHeader.biCompression == BI_BITFIELDS) {
    isOk = GetDIBits(screenDC, hbm, 0, 0, nullptr, (LPBITMAPINFO)bmi, DIB_RGB_COLORS) != 0;
  }
  if (!isOk) {
    // The error is read, and the exception made, before releaseDC calls
    // ReleaseDC(), which can change the error.
    StringStorage errMess;
    Environment::getErrStr(_T("Can't get a DIBits"), &errMess);
    throw Exception(errMess.getString());
  }
}

void Screen::fillPixelFormat(const BMI *bmi)
{
  memset(&m_pixelFormat, 0, sizeof(PixelFormat));

  m_pixelFormat.initBigEndianByNative();

  m_pixelFormat.bitsPerPixel = bmi->bmiHeader.biBitCount;

  if (bmi->bmiHeader.biCompression == BI_BITFIELDS) {
    // The shifts are at most 31.  The colour masks of a display are at
    // most 10 bits wide (in 30-bit colour), so the maximums fit 16 bits.
    m_pixelFormat.redShift   = static_cast<unsigned short>(findFirstBit(bmi->red));
    m_pixelFormat.greenShift = static_cast<unsigned short>(findFirstBit(bmi->green));
    m_pixelFormat.blueShift  = static_cast<unsigned short>(findFirstBit(bmi->blue));

    m_pixelFormat.redMax   = static_cast<unsigned short>(bmi->red    >> m_pixelFormat.redShift);
    m_pixelFormat.greenMax = static_cast<unsigned short>(bmi->green  >> m_pixelFormat.greenShift);
    m_pixelFormat.blueMax  = static_cast<unsigned short>(bmi->blue   >> m_pixelFormat.blueShift);

  } else {
    m_pixelFormat.bitsPerPixel = 32;
    m_pixelFormat.colorDepth = 24;
    m_pixelFormat.redMax = m_pixelFormat.greenMax = m_pixelFormat.blueMax = 0xff;
    m_pixelFormat.redShift   = 16;
    m_pixelFormat.greenShift = 8;
    m_pixelFormat.blueShift  = 0;
  }

  if (m_pixelFormat.bitsPerPixel == 32) {
    m_pixelFormat.colorDepth = 24;
  } else {
    m_pixelFormat.colorDepth = 16;
  }
}

int Screen::findFirstBit(const UINT32 bits)
{
  if (bits == 0) {
    return 0;
  }

  UINT32 b = bits;
  int shift;

  for (shift = 0; (b & 1) == 0; shift++) {
    b >>= 1;
  }

  return shift;
}

void Screen::fillScreenRect()
{
  m_virtDesktopRect.left = GetSystemMetrics(SM_XVIRTUALSCREEN);
  m_virtDesktopRect.top = GetSystemMetrics(SM_YVIRTUALSCREEN);
  m_virtDesktopRect.setWidth(GetSystemMetrics(SM_CXVIRTUALSCREEN));
  m_virtDesktopRect.setHeight(GetSystemMetrics(SM_CYVIRTUALSCREEN));
}
