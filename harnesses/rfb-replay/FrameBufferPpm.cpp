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

#include "FrameBufferPpm.h"

#include <stdio.h>

static char scaleComponent(UINT32 value, UINT32 max)
{
  return static_cast<char>((value * 255 + max / 2) / max);
}

std::vector<char> frameBufferToPpm(const FrameBuffer &fb)
{
  const Dimension dim = fb.getDimension();
  const PixelFormat pf = fb.getPixelFormat();
  const int bytesPerPixel = pf.bitsPerPixel / 8;

  char header[64];
  int headerLength = sprintf_s(header, sizeof(header), "P6\n%d %d\n255\n",
                               dim.width, dim.height);
  std::vector<char> ppm(header, header + headerLength);
  ppm.reserve(ppm.size() + static_cast<size_t>(dim.width) * dim.height * 3);

  for (int y = 0; y < dim.height; y++) {
    for (int x = 0; x < dim.width; x++) {
      const UINT8 *bytes = static_cast<const UINT8 *>(fb.getBufferPtr(x, y));
      UINT32 pixel = 0;
      for (int i = 0; i < bytesPerPixel; i++) {
        int shift = pf.bigEndian ? 8 * (bytesPerPixel - 1 - i) : 8 * i;
        pixel |= static_cast<UINT32>(bytes[i]) << shift;
      }
      ppm.push_back(scaleComponent((pixel >> pf.redShift) & pf.redMax, pf.redMax));
      ppm.push_back(scaleComponent((pixel >> pf.greenShift) & pf.greenMax, pf.greenMax));
      ppm.push_back(scaleComponent((pixel >> pf.blueShift) & pf.blueMax, pf.blueMax));
    }
  }
  return ppm;
}
