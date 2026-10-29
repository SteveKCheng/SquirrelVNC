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

#include "HexTileDecoder.h"

#include <algorithm>
#include <cstring>
#include <vector>

HexTileDecoder::HexTileDecoder(LogWriter *logWriter)
: DecoderOfRectangle(logWriter, EncodingDefs::HEXTILE)
{
}

HexTileDecoder::~HexTileDecoder()
{
}

Task<> HexTileDecoder::decode(AwaitableReader &reader,
                              FrameBuffer &framebuffer,
                              const Rect &dstRect,
                              std::mutex &fbLock)
{
  // shorcut
  const int bytesPerPixel = framebuffer.getBytesPerPixel();

  UINT32 background = 0;
  UINT32 foreground = 0;

  bool backgroundAccepted = false;

  // A tile as read: its raw pixels, or its subrectangles.
  std::vector<char> rawPixels;
  std::vector<SubRect> subRects;

  for (int y = dstRect.top; y < dstRect.bottom; y += TILE_SIZE) {
    for (int x = dstRect.left; x < dstRect.right; x += TILE_SIZE) {
      Rect tileRect(x,
                    y,
                    std::min(x + TILE_SIZE, dstRect.right),
                    std::min(y + TILE_SIZE, dstRect.bottom));

      if (!framebuffer.getDimension().getRect().intersection(tileRect).isEqualTo(tileRect))
        throw Exception(_T("Error in protocol: incorrect size of tile in hextile-decoder"));

      UINT8 flags = co_await reader.readUInt8();
      // If tile-coding is RAW.
      if (flags & 0x1) {
        const size_t bytesPerLine = tileRect.getWidth() * bytesPerPixel;
        rawPixels.resize(bytesPerLine * tileRect.getHeight());
        co_await reader.readFully(rawPixels.data(), rawPixels.size());

        std::lock_guard<std::mutex> lock(fbLock);
        for (int row = 0; row < tileRect.getHeight(); row++) {
          memcpy(framebuffer.getBufferPtr(tileRect.left, tileRect.top + row),
                 &rawPixels[row * bytesPerLine], bytesPerLine);
        }
      } else {
        if (flags & 0x2) {
          co_await reader.readFully(&background, bytesPerPixel);
          backgroundAccepted = true;
        }

        subRects.clear();
        if (flags & 0x4)
          co_await reader.readFully(&foreground, bytesPerPixel);

        if (flags & 0x8) {
          UINT8 numberOfSubrectangles = co_await reader.readUInt8();

          for (int i = 0; i < numberOfSubrectangles; i++) {

            if (flags & 0x10 && !(flags & 0x4))
              co_await reader.readFully(&foreground, bytesPerPixel);

            UINT8 xy = co_await reader.readUInt8();
            UINT8 wh = co_await reader.readUInt8();
            int subX = (xy >> 4) & 0xF;
            int subY = xy & 0xF;
            int w = ((wh >> 4) & 0xF) + 1;
            int h = (wh & 0xF) + 1;
            // Subrectangles are positioned relative to the tile, and must
            // lie within it; at the right and bottom edges a tile may be
            // smaller than 16 pixels.
            if (subX + w > tileRect.getWidth() || subY + h > tileRect.getHeight()) {
              throw Exception(_T("Error in protocol: Hextile subrectangle outside its tile"));
            }
            Rect subRect(subX, subY, subX + w, subY + h);

            subRect.move(tileRect.left, tileRect.top);
            subRects.push_back({subRect, foreground});
          }
        } else { // exist subrect
          if (!backgroundAccepted)
            throw Exception(_T("Server error in HexTile encoding: background color not accepted"));
        }

        std::lock_guard<std::mutex> lock(fbLock);
        if (backgroundAccepted)
          framebuffer.fillRect(tileRect, background);
        for (const SubRect &subRect : subRects) {
          framebuffer.fillRect(subRect.rect, subRect.colour);
        }
      } // it tile is not RAW
    } // for each tiles in line
  } // for each line of tile
}
