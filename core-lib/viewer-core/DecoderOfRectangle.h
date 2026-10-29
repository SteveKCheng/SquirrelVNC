// Copyright (C) 2012 GlavSoft LLC.
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

#ifndef _DECODER_OF_RECTANGLE_H_
#define _DECODER_OF_RECTANGLE_H_

#include "Decoder.h"

#include "io-lib/AwaitableReader.h"
#include "util/Task.h"

class DecoderOfRectangle : public Decoder
{
public:
  // Post-conditions: getCode() returns encoding.
  DecoderOfRectangle(LogWriter *logWriter, int encoding);
  ~DecoderOfRectangle() override;

  //
  // Reads the update of rect from reader, and draws it into frameBuffer
  // with decode().
  //
  // Threads: called on the thread that reads the connection, which is the
  // only thread that writes frameBuffer. Its pixels are drawn without a
  // lock, though another thread may be painting them: see
  // RemoteViewerCoreImpl::m_fbLock for why that is accepted.
  //
  // A coroutine (util/Task.h), as decode() is. The arguments must stay
  // valid until the Task has finished; awaiting it at once, as callers do,
  // ensures that.
  //
  // Pre-conditions: rect lies within frameBuffer.
  // Throws what decode() throws, which may have drawn part of rect.
  //
  virtual Task<> process(AwaitableReader &reader,
                         FrameBuffer &frameBuffer,
                         const Rect &rect);

protected:
  //
  // Reads the update of rect from reader and draws it into frameBuffer.
  //
  // A coroutine: each read is awaited.
  //
  virtual Task<> decode(AwaitableReader &reader,
                        FrameBuffer &frameBuffer,
                        const Rect &rect) = 0;
};

#endif
