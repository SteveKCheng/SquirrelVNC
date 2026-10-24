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

#ifndef _REPLAY_EVENTS_ADAPTER_H_
#define _REPLAY_EVENTS_ADAPTER_H_

#include "thread/LocalMutex.h"
#include "viewer-core/CoreEventsAdapter.h"
#include "win-system/WindowsEvent.h"

#include <string>
#include <vector>

class RemoteViewerCore;

//
// The CoreEventsAdapter of rfb-replay: records the events that come from
// the protocol thread as lines of text, and signals the end of the session.
//
// Events from the notifier thread (frame buffer updates and property
// changes) are not recorded, because when they come, and how they are
// merged, varies from run to run; the final frame buffer is compared
// instead (RemoteViewerCore::copyFrameBuffer()).
//
// Each line is an event name, followed for events that carry text by a
// space and the text in double quotes, with backslash, double quote and
// newline written as \\, \" and \n. Text is UTF-8. tests/gen/rfbgen.py
// writes the expected lines the same way.
//
class ReplayEventsAdapter final : public CoreEventsAdapter
{
public:
  ReplayEventsAdapter();
  ~ReplayEventsAdapter() override;

  //
  // Give the core whose events this adapter records, so that
  // onConnected() can log the desktop name from ServerInit as a
  // "desktop-name" event, just before "connected".
  //
  // Pre-conditions: called before core->start(), and core outlives the
  // session.
  //
  void setCore(RemoteViewerCore *core);

  void onBell() override;
  void onCutText(const StringStorage *cutText) override;
  void onEstablished() override;
  void onConnected(RfbOutputGate *output) override;
  void onDisconnect(const StringStorage *message) override;
  void onAuthError(const AuthException *exception) override;
  void onError(const Exception *exception) override;

  //
  // Wait until the session ends, by disconnection or error.
  //
  // Pre-conditions: none.
  // Post-conditions: returns true if the session has ended, false if it
  // had not ended within the given time.
  //
  bool waitForEnd(DWORD milliseconds);

  //
  // Pre-conditions: none; normally called after the session has ended.
  // Post-conditions: returns the events so far, each line ending in '\n'.
  //
  std::string getEventLog();

private:
  //
  // Post-conditions: name, as a line of its own, is added to the events.
  //
  void addEvent(const char *name);

  //
  // Pre-conditions: text is not null.
  // Post-conditions: a line of name and text is added to the events, the
  // text in UTF-8, quoted, with backslash, double quote and newline
  // escaped. Unpaired surrogates become U+FFFD.
  //
  void addEvent(const char *name, const StringStorage *text);

  RemoteViewerCore *m_core;
  LocalMutex m_lock;
  std::vector<std::string> m_events;
  bool m_hasEnded;
  WindowsEvent m_endEvent;
};

#endif // _REPLAY_EVENTS_ADAPTER_H_
