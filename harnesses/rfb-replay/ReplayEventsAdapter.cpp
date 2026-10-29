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

#include "util/TextEncoding.h"

#include <chrono>
#include <stdio.h>

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

void ReplayEventsAdapter::onCutText(const StringStorage &cutText)
{
  addEvent("cut-text", cutText);
}

void ReplayEventsAdapter::onEstablished()
{
  addEvent("established");
}

void ReplayEventsAdapter::onConnected(DataOutputStream * /*output*/,
                                      const ConnectionInfo &info)
{
  addEvent("desktop-name", info.desktopName);
  addEvent("connected");
}

void ReplayEventsAdapter::onDisconnect(const StringStorage &message)
{
  addEvent("disconnect", message);
  markEnded();
}

void ReplayEventsAdapter::onAuthError(const AuthException &exception)
{
  StringStorage message(exception.getMessage());
  addEvent("auth-error", message);
  markEnded();
}

void ReplayEventsAdapter::onError(const Exception &exception)
{
  StringStorage message(exception.getMessage());
  addEvent("error", message);
  markEnded();
}

void ReplayEventsAdapter::onFrameBufferUpdate(const Region & /* damage */)
{
}

void ReplayEventsAdapter::onFrameBufferPropChange(const Dimension & /* dimension */,
                                                  const PixelFormat & /* pixelFormat */)
{
}

void ReplayEventsAdapter::onCursorShape(const CursorImage &cursor)
{
  char text[64];
  sprintf_s(text, sizeof(text), "cursor %dx%d hot %d,%d",
            cursor.width, cursor.height, cursor.hotSpot.x, cursor.hotSpot.y);
  std::string line(text);
  for (UINT32 pixel : cursor.pixels) {
    sprintf_s(text, sizeof(text), " %08x", pixel);
    line += text;
  }
  addEvent(line.c_str());
}

bool ReplayEventsAdapter::waitForEnd(DWORD milliseconds)
{
  std::unique_lock<std::mutex> lock(m_lock);
  return m_ended.wait_for(lock, std::chrono::milliseconds(milliseconds),
                          [this] { return m_hasEnded; });
}

std::string ReplayEventsAdapter::getEventLog()
{
  std::lock_guard<std::mutex> lock(m_lock);
  std::string log;
  for (const std::string &event : m_events) {
    log += event;
    log += '\n';
  }
  return log;
}

void ReplayEventsAdapter::addEvent(const char *name)
{
  std::lock_guard<std::mutex> lock(m_lock);
  m_events.push_back(name);
}

void ReplayEventsAdapter::markEnded()
{
  {
    std::lock_guard<std::mutex> lock(m_lock);
    m_hasEnded = true;
  }
  m_ended.notify_all();
}

void ReplayEventsAdapter::addEvent(const char *name, const StringStorage &text)
{
  std::vector<std::byte> utf8 = TextEncoding::toUtf8(text);
  std::string line(name);
  line += " \"";
  for (std::byte b : utf8) {
    char c = static_cast<char>(b);
    switch (c) {
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
      line += c;
    }
  }
  line += '"';
  std::lock_guard<std::mutex> lock(m_lock);
  m_events.push_back(line);
}
