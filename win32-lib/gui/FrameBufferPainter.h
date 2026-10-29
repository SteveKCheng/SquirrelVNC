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

#ifndef _FRAME_BUFFER_PAINTER_H_
#define _FRAME_BUFFER_PAINTER_H_

#include "region/Rect.h"
#include "rfb/FrameBuffer.h"
#include "util/CommonHeader.h"

#include <vector>

//
// Paints a frame buffer, or part of it, into a device context with
// StretchDIBits(), straight from the frame buffer's memory.
//
// It keeps a buffer for frame buffers whose rows are not whole 32-bit
// words (8 or 16 bits per pixel, at some widths), which a DIB's rows must
// be: their rows are copied before painting.
//
// Threads: one thread at a time, as a window paints.
//
class FrameBufferPainter final
{
public:
  //
  // Paints the src part of fb at dst in hdc: copied if src and dst have
  // the same size, else scaled with HALFTONE.
  //
  // Pre-conditions: src lies within fb, and neither src nor dst is empty;
  // fb's pixel format is valid (PixelFormat::isValid()); the caller holds
  // whatever lock guards fb's pixels.
  // Throws Exception if GDI fails to paint.
  //
  void paint(HDC hdc, const FrameBuffer &fb, const Rect &src, const Rect &dst);

private:
  std::vector<BYTE> m_alignedRows;
};

#endif // _FRAME_BUFFER_PAINTER_H_
