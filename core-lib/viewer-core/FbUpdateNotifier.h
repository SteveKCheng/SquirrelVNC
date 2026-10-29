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

#ifndef _FB_UDPATE_NOTIFIER_H_
#define _FB_UPDATE_NOTIFIER_H_

#include "log-writer/LogWriter.h"
#include "rfb/FrameBuffer.h"
#include "region/Point.h"
#include "region/Rect.h"
#include "region/Region.h"
#include "thread/LocalMutex.h"
#include "thread/Thread.h"
#include "win-system/WindowsEvent.h"

class CoreEventsAdapter;

class FbUpdateNotifier : public Thread
{
public:
  FbUpdateNotifier(FrameBuffer *fb, LocalMutex *fbLock, LogWriter *logger);
  FbUpdateNotifier(const FbUpdateNotifier &) = delete;
  FbUpdateNotifier &operator=(const FbUpdateNotifier &) = delete;
  FbUpdateNotifier(FbUpdateNotifier &&) = delete;
  FbUpdateNotifier &operator=(FbUpdateNotifier &&) = delete;

  ~FbUpdateNotifier() override;

  void setAdapter(CoreEventsAdapter *adapter);

  void onUpdate(const Rect &rect);
  void onPropertiesFb();

protected:
  // Inherited from Thread
  void execute() override;
  void onTerminate() override;

  LocalMutex *m_fbLock;
  FrameBuffer *m_frameBuffer;

  // Pointer to adapter.
  // Nothing event (changing properties of frame buffer or update frame
  // buffer) don't sended to adapter, while m_adapter is 0.
  CoreEventsAdapter *m_adapter;

  LocalMutex m_updateLock;
  WindowsEvent m_eventUpdate;

  LogWriter *m_logWriter;

  // In this region added all updates of frame buffer.
  Region m_update;

  // This flag is true after call onPropertiesFb().
  bool m_isNewSize;
};

#endif
