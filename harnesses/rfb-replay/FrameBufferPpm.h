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

#ifndef _FRAME_BUFFER_PPM_H_
#define _FRAME_BUFFER_PPM_H_

#include "rfb/FrameBuffer.h"

#include <vector>

//
// Convert a frame buffer to a binary PPM image (P6), 8 bits per channel.
//
// Each colour component c, with maximum m in the frame buffer's pixel
// format, becomes (c * 255 + m / 2) / m in integers. tests/gen/rfbgen.py
// computes the expected images the same way. An empty frame buffer gives
// "P6\n0 0\n255\n".
//
// Pre-conditions: fb is not 0; if it is not empty, its pixel format is
// valid (PixelFormat::isValid()), so every maximum is at least 1.
// Post-conditions: returns the whole PPM file.
//
std::vector<char> frameBufferToPpm(const FrameBuffer *fb);

#endif // _FRAME_BUFFER_PPM_H_
