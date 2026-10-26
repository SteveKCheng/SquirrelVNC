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

#ifndef _REPLAY_AUTH_HANDLER_H_
#define _REPLAY_AUTH_HANDLER_H_

#include "viewer-core/VncAuthenticationHandler.h"

#include <thread>

//
// VNC authentication for rfb-replay: gives a fixed password instead of
// asking the user.
//
class ReplayAuthHandler final : public VncAuthenticationHandler
{
public:
  //
  // Pre-conditions: password is not 0.
  // Post-conditions: requestPassword() replies with a copy of password,
  // from a thread of its own, as the GUI thread replies in the viewer.
  //
  ReplayAuthHandler(const TCHAR *password);
  // Waits for the thread of the last reply.
  ~ReplayAuthHandler() override;

protected:
  void requestPassword(PasswordReply reply) override;
  void cancelPasswordRequest() noexcept override;

private:
  StringStorage m_password;
  // The thread that gives the last reply.
  std::thread m_replier;
};

#endif // _REPLAY_AUTH_HANDLER_H_
