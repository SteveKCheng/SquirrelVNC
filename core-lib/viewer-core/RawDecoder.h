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

#ifndef _RAW_DECODER_H_
#define _RAW_DECODER_H_

#include "DecoderOfRectangle.h"

class RawDecoder final : public DecoderOfRectangle
{
public:
  RawDecoder(LogWriter *logWriter);
  ~RawDecoder() override;

  //
  // This method is inherited from DecoderOfRectangle.
  //
  Task<> process(AwaitableReader &reader,
                 FrameBuffer &frameBuffer,
                 FrameBuffer &secondFrameBuffer,
                 const Rect &rect,
                 LocalMutex *fbLock,
                 FbUpdateNotifier *fbNotifier) override;

protected:
  Task<> decode(AwaitableReader &reader,
                FrameBuffer &frameBuffer,
                const Rect &rect) override;

private:
  static const size_t AREA_OF_ONE_PART = 1024 * 64;
};

#endif
