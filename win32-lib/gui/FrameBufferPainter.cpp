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

#include "FrameBufferPainter.h"

#include "util/Exception.h"
#include "win-system/Screen.h"

#include <cstring>

// The BITMAPINFO of rows of a frame buffer: colour masks, or for 8 bits
// per pixel a colour table.
union FrameBufferBitmapInfo
{
  Screen::BMI bitFields;
  Screen::Palette8bitBMI palette;
};

//
// Post-conditions: *info describes a top-down DIB of rows rows of width
// pixels in pixelFormat, for StretchDIBits().
// Pre-conditions: pixelFormat is valid (PixelFormat::isValid()).
//
static void setUpBitmapInfo(FrameBufferBitmapInfo *info, const PixelFormat &pixelFormat,
                            int width, int rows)
{
  *info = {};
  BITMAPINFOHEADER *header;
  if (pixelFormat.bitsPerPixel == 8) {
    for (unsigned int index = 0; index < 256; index++) {
      RGBQUAD &colour = info->palette.rgbQuad[index];
      colour.rgbRed = static_cast<BYTE>(((index >> pixelFormat.redShift) & pixelFormat.redMax) *
                                        0xFF / pixelFormat.redMax);
      colour.rgbGreen = static_cast<BYTE>(((index >> pixelFormat.greenShift) & pixelFormat.greenMax) *
                                          0xFF / pixelFormat.greenMax);
      colour.rgbBlue = static_cast<BYTE>(((index >> pixelFormat.blueShift) & pixelFormat.blueMax) *
                                         0xFF / pixelFormat.blueMax);
    }
    header = &info->palette.bmiHeader;
    header->biCompression = BI_RGB;
  } else {
    // In UINT32, since a mask in the top bits of a 32-bit pixel does not
    // fit in an int.
    info->bitFields.red = static_cast<UINT32>(pixelFormat.redMax) << pixelFormat.redShift;
    info->bitFields.green = static_cast<UINT32>(pixelFormat.greenMax) << pixelFormat.greenShift;
    info->bitFields.blue = static_cast<UINT32>(pixelFormat.blueMax) << pixelFormat.blueShift;
    header = &info->bitFields.bmiHeader;
    header->biCompression = BI_BITFIELDS;
  }
  header->biSize = sizeof(BITMAPINFOHEADER);
  header->biPlanes = 1;
  header->biBitCount = pixelFormat.bitsPerPixel;
  header->biWidth = width;
  // Top-down.
  header->biHeight = -rows;
}

void FrameBufferPainter::paint(HDC hdc, const FrameBuffer &fb, const Rect &src, const Rect &dst)
{
  // The DIB is the rows of src, whole, so that its top row is row 0
  // whichever way StretchDIBits() counts rows. It is as wide as the frame
  // buffer, not as its rows, so that GDI never sees the bytes past a row's
  // pixels, whatever HALFTONE reads around the source rectangle.
  const size_t pixelBytesPerRow =
    static_cast<size_t>(fb.getDimension().width) * fb.getBytesPerPixel();
  const size_t dibBytesPerRow = (pixelBytesPerRow + 3) / 4 * 4;
  const size_t bytesPerRow = fb.getBytesPerRow();
  const void *bits = fb.getBufferPtr(0, src.top);
  if (dibBytesPerRow != bytesPerRow) {
    m_alignedRows.resize(dibBytesPerRow * src.getHeight());
    const BYTE *rowPtr = static_cast<const BYTE *>(bits);
    for (int y = 0; y < src.getHeight(); y++, rowPtr += bytesPerRow) {
      memcpy(&m_alignedRows[y * dibBytesPerRow], rowPtr, pixelBytesPerRow);
    }
    bits = m_alignedRows.data();
  }
  FrameBufferBitmapInfo info;
  setUpBitmapInfo(&info, fb.getPixelFormat(), fb.getDimension().width, src.getHeight());

  const bool isScaled = src.getWidth() != dst.getWidth() ||
                        src.getHeight() != dst.getHeight();
  SetStretchBltMode(hdc, isScaled ? HALFTONE : COLORONCOLOR);
  if (isScaled) {
    // As SetStretchBltMode() asks after HALFTONE.
    SetBrushOrgEx(hdc, 0, 0, nullptr);
  }
  if (StretchDIBits(hdc, dst.left, dst.top, dst.getWidth(), dst.getHeight(),
                    src.left, 0, src.getWidth(), src.getHeight(),
                    bits, reinterpret_cast<const BITMAPINFO *>(&info),
                    DIB_RGB_COLORS, SRCCOPY) == 0) {
    throw Exception(_T("Cannot paint the frame buffer"));
  }
}
