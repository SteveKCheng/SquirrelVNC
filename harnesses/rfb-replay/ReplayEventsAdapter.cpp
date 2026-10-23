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

#include "ReplayEventsAdapter.h"

#include "thread/AutoLock.h"
#include "util/Utf8StringStorage.h"

ReplayEventsAdapter::ReplayEventsAdapter()
: m_hasEnded(false)
{
}

ReplayEventsAdapter::~ReplayEventsAdapter()
{
}

void ReplayEventsAdapter::onBell()
{
  addEvent("bell");
}

void ReplayEventsAdapter::onCutText(const StringStorage *cutText)
{
  addEvent("cut-text", cutText);
}

void ReplayEventsAdapter::onEstablished()
{
  addEvent("established");
}

void ReplayEventsAdapter::onConnected(RfbOutputGate * /*output*/)
{
  addEvent("connected");
}

void ReplayEventsAdapter::onDisconnect(const StringStorage *message)
{
  addEvent("disconnect", message);
  AutoLock al(&m_lock);
  m_hasEnded = true;
  m_endEvent.notify();
}

void ReplayEventsAdapter::onAuthError(const AuthException *exception)
{
  StringStorage message(exception->getMessage());
  addEvent("auth-error", &message);
  AutoLock al(&m_lock);
  m_hasEnded = true;
  m_endEvent.notify();
}

void ReplayEventsAdapter::onError(const Exception *exception)
{
  StringStorage message(exception->getMessage());
  addEvent("error", &message);
  AutoLock al(&m_lock);
  m_hasEnded = true;
  m_endEvent.notify();
}

bool ReplayEventsAdapter::waitForEnd(DWORD milliseconds)
{
  {
    AutoLock al(&m_lock);
    if (m_hasEnded) {
      return true;
    }
  }
  m_endEvent.waitForEvent(milliseconds);
  AutoLock al(&m_lock);
  return m_hasEnded;
}

std::string ReplayEventsAdapter::getEventLog()
{
  AutoLock al(&m_lock);
  std::string log;
  for (const std::string &event : m_events) {
    log += event;
    log += '\n';
  }
  return log;
}

void ReplayEventsAdapter::addEvent(const char *name)
{
  AutoLock al(&m_lock);
  m_events.push_back(name);
}

void ReplayEventsAdapter::addEvent(const char *name, const StringStorage *text)
{
  Utf8StringStorage utf8(text);
  std::string line(name);
  line += " \"";
  for (const char *p = utf8.getString(); *p != '\0'; p++) {
    switch (*p) {
    case '\\':
      line += "\\\\";
      break;
    case '"':
      line += "\\\"";
      break;
    case '\n':
      line += "\\n";
      break;
    default:
      line += *p;
    }
  }
  line += '"';
  AutoLock al(&m_lock);
  m_events.push_back(line);
}
