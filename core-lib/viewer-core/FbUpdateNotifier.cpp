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

#include "FbUpdateNotifier.h"

#include "thread/AutoLock.h"

#include "CoreEventsAdapter.h"

FbUpdateNotifier::FbUpdateNotifier(FrameBuffer *fb, LocalMutex *fbLock, LogWriter *logWriter)
: m_frameBuffer(fb),
  m_fbLock(fbLock),
  m_logWriter(logWriter),
  m_isNewSize(false),
  m_adapter(nullptr)
{
  resume();
}

FbUpdateNotifier::~FbUpdateNotifier()
{
  try {
    terminate();
    wait();
  } catch (...) {
  }
}

void FbUpdateNotifier::setAdapter(CoreEventsAdapter *adapter)
{
  AutoLock al(&m_updateLock);
  m_adapter = adapter;
  m_eventUpdate.notify();
}

void FbUpdateNotifier::execute()
{
  // Don't send any event to adapter, while adapter isn't set.
  {
    bool adapterIsNull = true;
    while (!isTerminating() && adapterIsNull) {
      // Wait event.
      m_eventUpdate.waitForEvent();

      // Check: now adapter is set?
      AutoLock al(&m_updateLock);
      if (m_adapter != nullptr) {
        adapterIsNull = false;
      }
    }
  }

  // Send event to adapter, while tread isn't terminated.
  while (!isTerminating()) {
    // If flag is set, then thread going to sleep (wait event).
    bool noUpdates = true;

    // Move updates to local variable with blocking notifier mutex "m_updateLock".
    bool isNewSize;
    Region update;
    {
      AutoLock al(&m_updateLock);
      isNewSize = m_isNewSize;
      m_isNewSize = false;

      update = m_update;
      m_update.clear();
    }

    // Send event "Change properties of frame buffer" to adapter
    // with blocking frame buffer mutex "m_fbLock".
    if (isNewSize) {
      noUpdates = false;
      m_logWriter->debug(_T("FbUpdateNotifier (event): new size of frame buffer"));
      try {
        AutoLock al(m_fbLock);
        m_adapter->onFrameBufferPropChange(*m_frameBuffer);
        // FIXME: it's bad code. Must work without one next line, but not it.
        Rect fbRect = m_frameBuffer->getDimension().getRect();
        m_adapter->onFrameBufferUpdate(*m_frameBuffer, fbRect);
      } catch (...) {
        m_logWriter->error(_T("FbUpdateNotifier (event): error in set new size"));
      }
    }

    // Send frame buffer update event to adapter
    // with blocking frame buffer mutex "m_fbLock".
    if (!update.isEmpty()) {
      noUpdates = false;

      AutoLock al(m_fbLock);
      std::vector<Rect> updateList;
      update.getRectVector(&updateList);
      m_logWriter->detail(_T("FbUpdateNotifier (event): %zu updates"), updateList.size());

      try {
        for (std::vector<Rect>::iterator i = updateList.begin(); i != updateList.end(); ++i) {
			m_adapter->onFrameBufferUpdate(*m_frameBuffer, *i);
        }
      } catch (...) {
        m_logWriter->error(_T("FbUpdateNotifier (event): error in update"));
      }
    }

    // Pause this thread, if there are no updates (cursor, frame buffer).
    if (noUpdates) {
      m_eventUpdate.waitForEvent();
    }
  }
}

void FbUpdateNotifier::onTerminate()
{
  m_eventUpdate.notify();
}

void FbUpdateNotifier::onUpdate(const Rect &update)
{
  {
    AutoLock al(&m_updateLock);
    m_update.addRect(update);
  }
  m_eventUpdate.notify();
  m_logWriter->debug(_T("FbUpdateNotifier: added rectangle"));
}

void FbUpdateNotifier::onPropertiesFb()
{
  {
    AutoLock al(&m_updateLock);
    m_update.clear();
    m_isNewSize = true;
  }
  m_eventUpdate.notify();
  m_logWriter->debug(_T("FbUpdateNotifier: new size of frame buffer"));
}

