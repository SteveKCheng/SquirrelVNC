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

#include "viewer-core/CoreEventsAdapter.h"

#include <condition_variable>
#include <mutex>
#include <string>
#include <vector>

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
// newline written as \\, \" and \n. Text is UTF-8. A cursor shape is
// the line "cursor WxH hot X,Y", then each pixel as a space and 8 hex
// digits (0xAARRGGBB, CursorImage). tests/gen/rfbgen.py writes the
// expected lines the same way.
//
// Threads: the callbacks come on the network thread; waitForEnd() and
// getEventLog() may be called on any other thread at the same time.
//
class ReplayEventsAdapter final : public CoreEventsAdapter
{
public:
  ReplayEventsAdapter();
  ~ReplayEventsAdapter() override;

  void onBell() override;
  void onCutText(const StringStorage &cutText) override;
  void onEstablished() override;
  //
  // Logs the desktop name from ServerInit as a "desktop-name" event, then
  // "connected".
  //
  void onConnected(DataOutputStream *output, const ConnectionInfo &info) override;
  void onDisconnect(const StringStorage &message) override;
  void onAuthError(const AuthException &exception) override;
  void onError(const Exception &exception) override;
  // Not recorded (see above).
  void onFrameBufferUpdate(const FrameBuffer &fb, const Rect &update) override;
  void onFrameBufferPropChange(const FrameBuffer &fb) override;
  // Logs a "cursor" event (see above).
  void onCursorShape(const CursorImage &cursor) override;

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
  // Post-conditions: a line of name and text is added to the events, the
  // text in UTF-8, quoted, with backslash, double quote and newline
  // escaped. Unpaired surrogates become U+FFFD.
  //
  void addEvent(const char *name, const StringStorage &text);

  //
  // Post-conditions: the session has ended; waitForEnd() returns true.
  //
  void markEnded();

  std::mutex m_lock;
  // Guarded by m_lock.
  std::vector<std::string> m_events;
  // Guarded by m_lock; m_ended is notified when it becomes true.
  bool m_hasEnded;
  std::condition_variable m_ended;
};

#endif // _REPLAY_EVENTS_ADAPTER_H_
