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

#include "DibSection.h"
#include "win-system/SystemException.h"

DibSection::DibSection(const PixelFormat *pf, const Dimension *dim, HWND compatibleWin)
: m_isOwnTargetDC(false),
  m_targetDC(0),
  m_memDC(0),
  m_hbmOld(0),
  m_hbmDIB(0),
  m_srcOffsetX(0),
  m_srcOffsetY(0),
  m_buffer(0)
{
  try {
    openDIBSection(pf, dim, compatibleWin);
  } catch (...) {
    closeDIBSection();
    throw;
  }
}

DibSection::~DibSection()
{
  try {
    closeDIBSection();
  } catch (...) {
  }
}

void DibSection::setTargetDC(HDC targetDC)
{
  releaseTargetDC();
  m_isOwnTargetDC = false;
  m_targetDC = targetDC;
}

void *DibSection::getBuffer()
{
  return m_buffer;
}

void DibSection::blitToDibSection(const Rect *rect)
{
  blitToDibSection(rect, SRCCOPY);
}

void DibSection::blitTransparentToDibSection(const Rect *rect)
{
  blitToDibSection(rect, SRCCOPY | CAPTUREBLT);
}

void DibSection::blitFromDibSection(const Rect *rect)
{
  blitFromDibSection(rect, SRCCOPY);
}

void DibSection::stretchFromDibSection(const Rect *srcRect, const Rect *dstRect)
{
  stretchFromDibSection(srcRect, dstRect, SRCCOPY);
}

void DibSection::blitToDibSection(const Rect *rect, DWORD flags)
{
  if (BitBlt(m_memDC, rect->left, rect->top, rect->getWidth(), rect->getHeight(),
             m_targetDC, rect->left + m_srcOffsetX,
             rect->top + m_srcOffsetY, flags) == 0) {
    throw Exception(_T("Can't blit to DIB section."));
  }
}

void DibSection::blitFromDibSection(const Rect *rect, DWORD flags)
{
  if (BitBlt(m_targetDC, rect->left + m_srcOffsetX, rect->top + m_srcOffsetY,
             rect->getWidth(), rect->getHeight(),
             m_memDC, rect->left, rect->top, flags) == 0) {
    throw Exception(_T("Can't blit from DIB section."));
  }
}

void DibSection::stretchFromDibSection(const Rect *srcRect, const Rect *dstRect, DWORD flags)
{
  SetStretchBltMode(m_targetDC, HALFTONE);
  if (StretchBlt(m_targetDC, srcRect->left + m_srcOffsetX, srcRect->top + m_srcOffsetY,
                 srcRect->getWidth(), srcRect->getHeight(),
                 m_memDC, dstRect->left, dstRect->top, dstRect->getWidth(), dstRect->getHeight(),
                 flags) == 0) {
    throw Exception(_T("Can't strech blit from DIB section."));
  }
}

void DibSection::setupPaletteBMI(Screen::Palette8bitBMI *bmi, const PixelFormat *pf,
                                 const Dimension *dim)
{
  *bmi = {};
  for (unsigned int index = 0; index < 256; index++) {
    unsigned int red = (index >> pf->redShift) & pf->redMax;
    red = red * 0xFF / pf->redMax;
    bmi->rgbQuad[index].rgbRed = (BYTE)red;
    unsigned int green = (index >> pf->greenShift) & pf->greenMax;
    green = green * 0xFF / pf->greenMax;
    bmi->rgbQuad[index].rgbGreen = (BYTE)green;
    unsigned int blue = (index >> pf->blueShift) & pf->blueMax;
    blue = blue * 0xFF / pf->blueMax;
    bmi->rgbQuad[index].rgbBlue = (BYTE)blue;
  }
  setupBMIHeader(&bmi->bmiHeader, pf, dim);
}

void DibSection::setupBitFieldsBMI(Screen::BMI *bmi, const PixelFormat *pf,
                                   const Dimension *dim)
{
  *bmi = {};
  bmi->bmiHeader.biCompression = BI_BITFIELDS;
  bmi->red   = pf->redMax   << pf->redShift;
  bmi->green = pf->greenMax << pf->greenShift;
  bmi->blue  = pf->blueMax  << pf->blueShift;
  setupBMIHeader(&bmi->bmiHeader, pf, dim);
}

void DibSection::setupBMIHeader(BITMAPINFOHEADER *header, const PixelFormat *pf,
                                const Dimension *dim)
{
  header->biSize = sizeof(BITMAPINFOHEADER);
  header->biPlanes = 1;
  header->biBitCount = pf->bitsPerPixel;
  header->biWidth = dim->width;
  header->biHeight = -dim->height;
}

void DibSection::openDIBSection(const PixelFormat *pf, const Dimension *dim, HWND compatibleWin)
{
  m_targetDC = GetDC(compatibleWin);
  m_isOwnTargetDC = true;
  if (m_targetDC == 0) {
    throw SystemException(_T("Can't get DC to create a DIB section"));
  }
  if (compatibleWin == 0) {
    // In this special case is needed to store offset of the desktop, because coordinates
    // of the top level corner may be non zero.
    m_screen.update();
    Rect deskRect = m_screen.getDesktopRect();
    m_srcOffsetX = deskRect.left;
    m_srcOffsetY = deskRect.top;
  }

  // CreateDIBSection() takes a BITMAPINFO, which declares a colour table of
  // one entry. Each structure here is a BITMAPINFO with the colour table or
  // the masks its kind of DIB section needs, so it is converted to
  // BITMAPINFO only to be passed to GDI.
  Screen::BMI bitFieldBmi;
  Screen::Palette8bitBMI paletteBmi;
  const BITMAPINFO *pBmi;

  if (pf->bitsPerPixel == 8) {
    setupPaletteBMI(&paletteBmi, pf, dim);
    pBmi = reinterpret_cast<const BITMAPINFO *>(&paletteBmi);
  } else {
    setupBitFieldsBMI(&bitFieldBmi, pf, dim);
    pBmi = reinterpret_cast<const BITMAPINFO *>(&bitFieldBmi);
  }

  m_memDC = CreateCompatibleDC(m_targetDC);
  if (m_memDC == NULL) {
    throw SystemException(_T("Can't create a compatible DC to open a dib section"));
  }

  m_hbmDIB = CreateDIBSection(m_memDC, pBmi, DIB_RGB_COLORS, &m_buffer, NULL, NULL);
  if (m_hbmDIB == 0) {
    throw SystemException(_T("Can't create a dib section"));
  }

  m_hbmOld = (HBITMAP)SelectObject(m_memDC, m_hbmDIB);
}

void DibSection::closeDIBSection()
{
  if (m_hbmOld != 0) {
    SelectObject(m_memDC, m_hbmOld);
    m_hbmOld = 0;
  }

  if (m_hbmDIB != 0) {
    DeleteObject(m_hbmDIB);
    m_hbmDIB = 0;
  }

  if (m_memDC != 0) {
    DeleteDC(m_memDC);
    m_memDC = 0;
  }

  releaseTargetDC();
}

void DibSection::releaseTargetDC()
{
  if (m_targetDC != 0 && m_isOwnTargetDC) {
    ReleaseDC(0, m_targetDC);
    m_targetDC = 0;
  }
}
