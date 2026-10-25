// Copyright (C) 2012 GlavSoft LLC.
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

#ifndef __DIBSECTION_H__
#define __DIBSECTION_H__

#include "util/CommonHeader.h"
#ifndef CAPTUREBLT
#define CAPTUREBLT          (DWORD)0x40000000 /* Include layered windows */
#endif
#include "win-system/Screen.h"

// This clas is a primitive wrapper to a DIB section.
class DibSection final
{
public:
  // Note that if the compatibleWin doesn't specify or is zero the class will create an
  // DC for the entire screen. In this case an this object owner must provide
  // destructor calling from the same thread that called the constructor.
  // In default the created DC will used as a target DC for the blitting operations.
  // It may be changed many times later. Note that changed DC must be compatible with
  // the DIB section.
  DibSection(const PixelFormat &pf, const Dimension &dim, HWND compatibleWin = nullptr);
  DibSection(const DibSection &) = delete;
  DibSection &operator=(const DibSection &) = delete;
  DibSection(DibSection &&) = delete;
  DibSection &operator=(DibSection &&) = delete;

  ~DibSection();

  /**
   * Changes the target DC: the DC that the blit and stretch functions copy
   * to and from. Until this is called, it is the DC of the window given
   * when the DIB section was made, or the whole screen's DC when none was
   * given; for the screen, coordinates are relative to the top-left
   * corner of the virtual desktop. May be called any number of times.
   */
  void setTargetDC(HDC targetDC);

  void *getBuffer();

  // This function copies a block of bits from the target DC (see setTargetDC()) to the
  // DIB section.
  // Note that this function does not copy any transparent windows.
  // This function throwing an exception on a failure.
  void blitToDibSection(const Rect &rect);

  // This function copies a block of bits from the target DC (see setTargetDC()) to the
  // DIB section.
  // Note that this function copies transparent windows too.
  // This function throwing an exception on a failure.
  void blitTransparentToDibSection(const Rect &rect);

  // This function copies a block of bits from the DIB section to the target DC (see
  // setTargetDC()).
  // Note that this function does not copy any transparent windows.
  // This function throwing an exception on a failure.
  void blitFromDibSection(const Rect &rect);

  /**
   * Copies a block of pixels from the DIB section to the target DC (see
   * setTargetDC()), stretching it from srcRect to dstRect.
   * Does not copy transparent windows.
   * @param dstRect the area of the target DC to draw.
   * @param srcRect the area of the DIB section to copy.
   * @throw Exception on failure.
   */
  void stretchFromDibSection(const Rect &dstRect, const Rect &srcRect);

private:
  // Opens a new DIB section.
  // If targetDC == 0 the function will use a current desktop DC.
  // Pre-conditions: pf->isValid(). The format may come from the server
  // and is checked where it is read (RemoteViewerCore::readPixelFormat()).
  void openDIBSection(const PixelFormat &pf, const Dimension &dim, HWND compatibleWin);
  void closeDIBSection();
  void releaseTargetDC();

  // This function throwing an exception on a failure.
  void blitToDibSection(const Rect &rect, DWORD flags);

  // This function throwing an exception on a failure.
  void blitFromDibSection(const Rect &rect, DWORD flags);
  void stretchFromDibSection(const Rect &dstRect, const Rect &srcRect, DWORD flags);

  // Fills *bmi to describe an 8-bit DIB section of the given format and
  // size, with a colour table that maps each pixel value to its colour.
  // Pre-conditions: pf->bitsPerPixel == 8; pf->isValid().
  // Post-conditions: every field of *bmi is set; no other state changes.
  void setupPaletteBMI(Screen::Palette8bitBMI *bmi, const PixelFormat &pf,
                       const Dimension &dim);

  // Fills *bmi to describe a BI_BITFIELDS DIB section of the given format
  // and size, with the colour masks of the format.
  // Pre-conditions: pf->bitsPerPixel is 16 or 32; pf->isValid().
  // Post-conditions: every field of *bmi is set; no other state changes.
  void setupBitFieldsBMI(Screen::BMI *bmi, const PixelFormat &pf,
                         const Dimension &dim);

  // Sets the header fields that do not depend on the kind of DIB section:
  // the size, planes, bit count and dimensions (top-down rows).
  // Pre-conditions: the other fields of *header are already set.
  // Post-conditions: biSize, biPlanes, biBitCount, biWidth and biHeight
  // are set.
  void setupBMIHeader(BITMAPINFOHEADER *header, const PixelFormat &pf,
                      const Dimension &dim);

  bool m_isOwnTargetDC;
  HDC m_targetDC;
  HDC m_memDC;
  HBITMAP m_hbmOld;
  HBITMAP m_hbmDIB;
  // Coordinates of the source dc can be negative.
  int m_srcOffsetX;
  int m_srcOffsetY;

  void *m_buffer;

  Screen m_screen;
};

#endif // __DIBSECTION_H__
