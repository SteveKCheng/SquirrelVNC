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

#include "CursorImage.h"

#include "RfbParsing.h"

#include <cstring>

// A component of maximum max, scaled to 0 to 255, rounded to nearest, as
// rfb-replay's PPM output does.
static UINT32 scaleComponent(UINT32 value, UINT32 max)
{
  return (value * 255 + max / 2) / max;
}

CursorImage CursorImage::fromRichCursor(const RfbParsing::RichCursor &cursor,
                                        const PixelFormat &pixelFormat)
{
  CursorImage image;
  image.hotSpot = cursor.hotSpot;
  if (cursor.pixels.empty()) {
    return image;
  }
  image.width = cursor.width;
  image.height = cursor.height;
  image.pixels.resize(static_cast<size_t>(image.width) * image.height);

  const size_t bytesPerPixel = pixelFormat.bitsPerPixel / 8;
  const size_t maskBytesPerRow = (image.width + 7) / 8;
  for (int y = 0; y < image.height; y++) {
    for (int x = 0; x < image.width; x++) {
      const size_t i = static_cast<size_t>(y) * image.width + x;
      const UINT8 maskByte = cursor.bitmask[y * maskBytesPerRow + x / 8];
      if ((maskByte & (0x80 >> (x % 8))) == 0) {
        continue;
      }
      // In the frame buffer's byte order, as the decoders store pixels.
      UINT32 pixel = 0;
      memcpy(&pixel, &cursor.pixels[i * bytesPerPixel], bytesPerPixel);
      const UINT32 red = scaleComponent((pixel >> pixelFormat.redShift) & pixelFormat.redMax,
                                        pixelFormat.redMax);
      const UINT32 green = scaleComponent((pixel >> pixelFormat.greenShift) & pixelFormat.greenMax,
                                          pixelFormat.greenMax);
      const UINT32 blue = scaleComponent((pixel >> pixelFormat.blueShift) & pixelFormat.blueMax,
                                         pixelFormat.blueMax);
      image.pixels[i] = 0xFF000000 | red << 16 | green << 8 | blue;
    }
  }
  return image;
}
