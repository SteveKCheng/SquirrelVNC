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

#include "RreDecoder.h"

RreDecoder::RreDecoder(LogWriter *logWriter)
: DecoderOfRectangle(logWriter, EncodingDefs::RRE)
{
}

RreDecoder::~RreDecoder()
{
}

Task<> RreDecoder::decode(AwaitableReader &reader,
                          FrameBuffer &frameBuffer,
                          const Rect &dstRect)
{
  UINT32 numberRectangle = co_await reader.readUInt32();
  size_t bytesPerPixel = frameBuffer.getBytesPerPixel();

  UINT32 backgroundColor;
  co_await reader.readFully(&backgroundColor, bytesPerPixel);
  frameBuffer.fillRect(dstRect, backgroundColor);

  while (numberRectangle--) {
    UINT32 color;
    co_await reader.readFully(&color, bytesPerPixel);
    UINT32 x = co_await reader.readUInt16();
    UINT32 y = co_await reader.readUInt16();
    UINT32 w = co_await reader.readUInt16();
    UINT32 h = co_await reader.readUInt16();

    // Subrectangles are positioned relative to the rectangle, and must lie
    // within it.
    if (x + w > static_cast<UINT32>(dstRect.getWidth()) ||
        y + h > static_cast<UINT32>(dstRect.getHeight())) {
      throw Exception(_T("Error in protocol: RRE subrectangle outside its rectangle"));
    }

    Rect rect(x, y, x + w, y + h);
    rect.move(dstRect.left, dstRect.top);
    frameBuffer.fillRect(rect, color);
  }
}
