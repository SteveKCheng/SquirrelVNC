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
// Unit tests of FrameBufferPainter: painting frame buffers of each pixel
// size into a 32-bit DIB section, and reading back what GDI drew.
//

#include "UnitTest.h"

#include "gui/FrameBufferPainter.h"
#include "rfb/StandardPixelFormatFactory.h"
#include "util/Exception.h"

#include <cstring>

namespace
{
  // A memory DC drawing into a top-down 32-bit DIB of width x height,
  // filled with fill.
  class Target
  {
  public:
    Target(int width, int height, UINT32 fill)
    : m_width(width)
    {
      BITMAPINFO info = {};
      info.bmiHeader.biSize = sizeof(info.bmiHeader);
      info.bmiHeader.biWidth = width;
      info.bmiHeader.biHeight = -height;
      info.bmiHeader.biPlanes = 1;
      info.bmiHeader.biBitCount = 32;
      info.bmiHeader.biCompression = BI_RGB;
      m_dc = CreateCompatibleDC(nullptr);
      void *bits = nullptr;
      m_bitmap = CreateDIBSection(m_dc, &info, DIB_RGB_COLORS, &bits, nullptr, 0);
      m_pixels = static_cast<UINT32 *>(bits);
      m_old = SelectObject(m_dc, m_bitmap);
      for (int i = 0; i < width * height; i++) {
        m_pixels[i] = fill;
      }
    }
    Target(const Target &) = delete;
    Target &operator=(const Target &) = delete;
    ~Target()
    {
      SelectObject(m_dc, m_old);
      DeleteObject(m_bitmap);
      DeleteDC(m_dc);
    }

    bool isValid() const { return m_dc != nullptr && m_pixels != nullptr; }
    HDC dc() const { return m_dc; }
    // The 0xRRGGBB of a pixel.
    UINT32 at(int x, int y)
    {
      GdiFlush();
      return m_pixels[y * m_width + x] & 0xFFFFFF;
    }

  private:
    int m_width;
    HDC m_dc;
    HBITMAP m_bitmap;
    HGDIOBJ m_old;
    UINT32 *m_pixels;
  };

  bool isNear(UINT32 a, UINT32 b)
  {
    for (int shift = 0; shift < 24; shift += 8) {
      int ca = (a >> shift) & 0xFF;
      int cb = (b >> shift) & 0xFF;
      if (ca - cb > 8 || cb - ca > 8) {
        return false;
      }
    }
    return true;
  }

  // A 32-bit frame buffer whose pixel at (x, y) is red y, green x, blue 0x40.
  void fill32(FrameBuffer &fb)
  {
    for (int y = 0; y < fb.getDimension().height; y++) {
      for (int x = 0; x < fb.getDimension().width; x++) {
        UINT32 pixel = static_cast<UINT32>(y) << 16 | static_cast<UINT32>(x) << 8 | 0x40;
        memcpy(fb.getBufferPtr(x, y), &pixel, sizeof(pixel));
      }
    }
  }
}

void runFrameBufferPainterTests()
{
  FrameBufferPainter painter;

  // 32 bits per pixel, part of it, 1:1: rows in order, from the right one.
  {
    FrameBuffer fb;
    fb.setProperties(Dimension(7, 5), StandardPixelFormatFactory::create32bppPixelFormat());
    fill32(fb);
    Target target(8, 6, 0xFFFFFF);
    check(target.isValid(), "FrameBufferPainter: the test's target DC is made");
    painter.paint(target.dc(), fb, Rect(2, 1, 6, 4), Rect(1, 2, 5, 5));
    bool same = true;
    for (int y = 0; y < 3; y++) {
      for (int x = 0; x < 4; x++) {
        UINT32 expected = static_cast<UINT32>(1 + y) << 16 | static_cast<UINT32>(2 + x) << 8 | 0x40;
        same = same && target.at(1 + x, 2 + y) == expected;
      }
    }
    check(same, "FrameBufferPainter paints a part of a 32-bit frame buffer 1:1, rows in order");
    check(target.at(0, 0) == 0xFFFFFF && target.at(5, 2) == 0xFFFFFF && target.at(1, 5) == 0xFFFFFF,
          "FrameBufferPainter paints nothing outside the destination");
  }

  // 8 bits per pixel, a width whose rows are not whole 32-bit words: each
  // row is copied, and the colour table gives the colours.
  {
    FrameBuffer fb;
    fb.setProperties(Dimension(5, 3), StandardPixelFormatFactory::create8bppPixelFormat());
    // Red is 3 bits at shift 0, green 3 at 3, blue 2 at 6.
    const UINT8 values[] = {0x07, 0x38, 0xC0, 0xFF, 0x00};
    const UINT32 colours[] = {0xFF0000, 0x00FF00, 0x0000FF, 0xFFFFFF, 0x000000};
    for (int y = 0; y < 3; y++) {
      for (int x = 0; x < 5; x++) {
        *static_cast<UINT8 *>(fb.getBufferPtr(x, y)) = values[(x + y) % 5];
      }
    }
    Target target(5, 3, 0x123456);
    painter.paint(target.dc(), fb, Rect(0, 0, 5, 3), Rect(0, 0, 5, 3));
    bool same = true;
    for (int y = 0; y < 3; y++) {
      for (int x = 0; x < 5; x++) {
        same = same && target.at(x, y) == colours[(x + y) % 5];
      }
    }
    check(same, "FrameBufferPainter paints an 8-bit frame buffer of an odd width");

    // Only the second and third rows.
    Target part(5, 2, 0x123456);
    painter.paint(part.dc(), fb, Rect(0, 1, 5, 3), Rect(0, 0, 5, 2));
    same = true;
    for (int y = 0; y < 2; y++) {
      for (int x = 0; x < 5; x++) {
        same = same && part.at(x, y) == colours[(x + y + 1) % 5];
      }
    }
    check(same, "FrameBufferPainter paints rows from the middle of an 8-bit frame buffer");
  }

  // 16 bits per pixel (red 5, green 6, blue 5), an odd width.
  {
    FrameBuffer fb;
    fb.setProperties(Dimension(3, 2), StandardPixelFormatFactory::create16bppPixelFormat());
    const UINT16 values[] = {0xF800, 0x07E0, 0x001F};
    const UINT32 colours[] = {0xFF0000, 0x00FF00, 0x0000FF};
    for (int y = 0; y < 2; y++) {
      for (int x = 0; x < 3; x++) {
        memcpy(fb.getBufferPtr(x, y), &values[(x + y) % 3], sizeof(UINT16));
      }
    }
    Target target(3, 2, 0x123456);
    painter.paint(target.dc(), fb, Rect(0, 0, 3, 2), Rect(0, 0, 3, 2));
    bool same = true;
    for (int y = 0; y < 2; y++) {
      for (int x = 0; x < 3; x++) {
        same = same && isNear(target.at(x, y), colours[(x + y) % 3]);
      }
    }
    check(same, "FrameBufferPainter paints a 16-bit frame buffer of an odd width");
  }

  // Scaled: twice the size, a frame buffer of one colour stays that colour.
  {
    FrameBuffer fb;
    fb.setProperties(Dimension(4, 4), StandardPixelFormatFactory::create32bppPixelFormat());
    fb.setColor(0x20, 0x80, 0xC0);
    Target target(8, 8, 0xFFFFFF);
    painter.paint(target.dc(), fb, Rect(0, 0, 4, 4), Rect(0, 0, 8, 8));
    bool same = true;
    for (int y = 0; y < 8; y++) {
      for (int x = 0; x < 8; x++) {
        same = same && isNear(target.at(x, y), 0x2080C0);
      }
    }
    check(same, "FrameBufferPainter scales a frame buffer to twice its size");
  }
}
