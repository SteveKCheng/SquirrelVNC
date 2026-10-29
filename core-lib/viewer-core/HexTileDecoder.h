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

#ifndef _HEX_TILE_DECODER_H_
#define _HEX_TILE_DECODER_H_

#include "DecoderOfRectangle.h"

class HexTileDecoder final : public DecoderOfRectangle
{
public:
  HexTileDecoder(LogWriter *logWriter);
  ~HexTileDecoder() override;

protected:
  //
  // Reads a Hextile-encoded rectangle from input and draws it into
  // framebuffer at dstRect, tile by tile.
  //
  // dstRect comes from the server, so it is not trusted: each 16x16 tile
  // must lie inside framebuffer, or an Exception is thrown.  An Exception
  // is also thrown for a tile that has no subrectangles before any
  // background colour was sent, and for a subrectangle that does not lie
  // within its tile (tiles at the right and bottom edges may be smaller
  // than 16 pixels).  On an exception, the tiles before the bad one have
  // already been drawn, and nothing of the bad one.
  //
  // Each tile is read whole, then drawn under fbLock.
  //
  // Pre: framebuffer has the pixel format the server sends, so that each
  // colour read is framebuffer->getBytesPerPixel() bytes.
  //
  Task<> decode(AwaitableReader &reader,
                FrameBuffer &framebuffer,
                const Rect &dstRect,
                std::mutex &fbLock) override;
private:
  static const int TILE_SIZE = 16;

  // A subrectangle of a tile as read, before it is drawn.
  struct SubRect
  {
    Rect rect;
    UINT32 colour;
  };
};

#endif
