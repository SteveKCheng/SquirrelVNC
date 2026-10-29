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

#ifndef _FRAME_BUFFER_ACCESS_H_
#define _FRAME_BUFFER_ACCESS_H_

#include "rfb/FrameBuffer.h"

#include <mutex>

//
// Read access to a connection's frame buffer, holding the frame buffer's
// lock for as long as it lives (RemoteViewerCore::accessFrameBuffer()).
// The decoders wait to draw until it is gone, so a holder sees whole
// rectangles, and the frame buffer is not replaced meanwhile (a change of
// size or pixel format).
//
// Hold one only briefly, as a paint does, and never on the network
// thread, which takes the lock itself. Movable, not copyable.
//
class FrameBufferAccess final
{
public:
  //
  // Pre-conditions: lock holds the mutex that guards frameBuffer, which
  // outlives this object.
  //
  FrameBufferAccess(std::unique_lock<std::mutex> lock, const FrameBuffer &frameBuffer)
  : m_lock(std::move(lock)),
    m_frameBuffer(&frameBuffer)
  {
  }

  const FrameBuffer &get() const { return *m_frameBuffer; }

private:
  std::unique_lock<std::mutex> m_lock;
  const FrameBuffer *m_frameBuffer;
};

#endif // _FRAME_BUFFER_ACCESS_H_
