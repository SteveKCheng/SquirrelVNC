// Copyright (C) 2013 GlavSoft LLC.
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

#include "ViewerVncAuthHandler.h"
#include "ViewerWindow.h"

ViewerVncAuthHandler::ViewerVncAuthHandler(ConnectionData *connectionData,
                                           ViewerWindow &passwordPrompt)
: m_connectionData(connectionData),
  m_passwordPrompt(passwordPrompt)
{
}

ViewerVncAuthHandler::~ViewerVncAuthHandler()
{
}

void ViewerVncAuthHandler::requestPassword(PasswordReply reply)
{
  if (m_connectionData->isSetPassword()) {
    reply(m_connectionData->getPlainPassword());
    return;
  }
  m_passwordPrompt.requestPassword([this, reply](std::optional<StringStorage> password) {
    // On the GUI thread, which also reads m_connectionData (to reconnect).
    if (password) {
      m_connectionData->setPlainPassword(*password);
    }
    reply(std::move(password));
  });
}

void ViewerVncAuthHandler::cancelPasswordRequest() noexcept
{
  m_passwordPrompt.cancelPasswordRequest();
}
