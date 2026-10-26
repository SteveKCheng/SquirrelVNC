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

#include "ReplayAuthHandler.h"

ReplayAuthHandler::ReplayAuthHandler(const TCHAR *password)
: m_password(password)
{
}

ReplayAuthHandler::~ReplayAuthHandler()
{
  if (m_replier.joinable()) {
    m_replier.join();
  }
}

void ReplayAuthHandler::requestPassword(PasswordReply reply)
{
  if (m_replier.joinable()) {
    m_replier.join();
  }
  m_replier = std::thread([reply, password = m_password] {
    reply(password);
  });
}

void ReplayAuthHandler::cancelPasswordRequest() noexcept
{
  // The reply comes without waiting for anything: nothing to cancel.
}
