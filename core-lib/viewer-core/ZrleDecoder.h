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

#ifndef _ZRLE_DECODER_H_
#define _ZRLE_DECODER_H_

#include "DecoderOfRectangle.h"

#include "io-lib/DataInputStream.h"
#include "util/Inflater.h"

class ZrleDecoder : public DecoderOfRectangle
{
public:
  ZrleDecoder(LogWriter *logWriter);
  ~ZrleDecoder() override;

protected:
  typedef std::vector<unsigned int> Palette;

protected:
  // Decodes a ZRLE rectangle into frameBuffer, in the frame buffer's pixel
  // format.  Throws Exception for data that does not fit dstRect or the
  // frame buffer, and for a 32-bit pixel format whose colours are in the
  // high 3 bytes, which is not supported.
  // Pre-condition: the frame buffer's pixel format is valid
  // (PixelFormat::isValid()).
  void decode(RfbInputGate *input,
              FrameBuffer *frameBuffer,
              const Rect *dstRect) override;


  void readAndInflate(RfbInputGate *input, size_t maximalUnpackedSize);

  int readType(DataInputStream *input);

  size_t readRunLength(DataInputStream *input);

  void readPalette(DataInputStream *input,
                   const int paletteSize,
                   Palette *palette);

  void readRawTile(DataInputStream *input,
                   std::vector<char> &pixels,
                   const Rect *tileRect);

  void readSolidTile(DataInputStream *input,
                     std::vector<char> &pixels,
                     const Rect *tileRect);

  void readPackedPaletteTile(DataInputStream *input,
                             std::vector<char> &pixels,
                             const Rect *tileRect,
                             const int type);

  void readPlainRleTile(DataInputStream *input,
                        std::vector<char> &pixels,
                        const Rect *tileRect);

  void readPaletteRleTile(DataInputStream *input,
                          std::vector<char> &pixels,
                          const Rect *tileRect,
                          const int type);


  void drawTile(FrameBuffer *fb,
                const Rect *tileRect,
                const std::vector<char> *pixels);

  Inflater m_inflater;
  size_t m_bytesPerPixel;

private:
  static const int TILE_SIZE = 64;

  // maximal size of tile (64x64) is max value from follow:
  // 1. size of raw: subenc + data:
  //    1 + width * height * pixelSize = 1 + 64 * 64 * 4 = 16385
  // 2. size of solid: subenc + solidColor:
  //    1 + pixelSize = 1 + 4 = 5
  // 3. size of packpalette: subenc + palleteSize + data:
  //    1 + paletteSize * pixelSize + m = 1 + 16 * 4 + 32 * 64 = 2113
  // 4. size of plainRle: subenc + rle * width * height:
  //    1 + (pixelSize + 1) * width * height = 1 + (4 + 1) * 64 * 64 = 20481
  // 5. size of paletteRle: subenc + paletteSzie + data:
  //    1 + paletteSize * pixelSize + rle * width * height = 1 + 128 * 4 + (1 + 1) * 64 * 64 = 8705
  static const size_t MAXIMAL_TILE_SIZE = 20481;
  static const size_t TILE_LENGTH_SIZE = sizeof(UINT32);

  static size_t getMaxSizeOfRectangle(const Rect *dstRect);
};

#endif
