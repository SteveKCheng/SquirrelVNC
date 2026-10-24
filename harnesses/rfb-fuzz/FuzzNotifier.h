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

#ifndef _FUZZ_NOTIFIER_H_
#define _FUZZ_NOTIFIER_H_

#include "thread/AutoLock.h"
#include "viewer-core/FbUpdateNotifier.h"

//
// The FbUpdateNotifier that DecoderOfRectangle::process() needs, for a
// program that never starts it.
//
// The notifier's thread is created suspended and stays that way, so
// onUpdate() only adds to a region and nothing reports updates. A
// FbUpdateNotifier that is never started cannot be destroyed without
// leaking its suspended thread, so one object serves a whole fuzzing run,
// and clearUpdates() empties its region between inputs.
//
class FuzzNotifier final : public FbUpdateNotifier
{
public:
  //
  // Pre-conditions: fb, fbLock and logWriter outlive this object.
  //
  FuzzNotifier(FrameBuffer *fb, LocalMutex *fbLock, LogWriter *logWriter)
  : FbUpdateNotifier(fb, fbLock, logWriter)
  {
  }

  //
  // Post-conditions: the region of updates is empty.
  //
  void clearUpdates()
  {
    AutoLock al(&m_updateLock);
    m_update.clear();
  }
};

#endif // _FUZZ_NOTIFIER_H_
