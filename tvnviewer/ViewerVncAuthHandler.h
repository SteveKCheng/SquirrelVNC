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

#ifndef __VIEWERVNCAUTHHANDLER_H__
#define __VIEWERVNCAUTHHANDLER_H__

#include "viewer-core/VncAuthenticationHandler.h"
#include "ConnectionData.h"

class ViewerWindow;

class ViewerVncAuthHandler final : public VncAuthenticationHandler
{
public:
  //
  // The password is taken from connectionData if it has one; otherwise the
  // user is asked through passwordPrompt, and the answer stored in
  // connectionData. Both must outlive the handler's use by the network
  // thread (ViewerInstance deletes them only once its connection has
  // finished).
  //
  ViewerVncAuthHandler(ConnectionData *connectionData,
                       ViewerWindow &passwordPrompt);
  ~ViewerVncAuthHandler() override;

private:
  //
  // Called on the network thread. Replies at once if m_connectionData has
  // the password; otherwise asks the user (ViewerWindow::requestPassword()),
  // stores the answer in m_connectionData, on the GUI thread, and replies
  // with it.
  //
  void requestPassword(PasswordReply reply) override;

  //
  // Cancels the user's prompt (ViewerWindow::cancelPasswordRequest()).
  //
  void cancelPasswordRequest() noexcept override;

  ConnectionData *m_connectionData;
  ViewerWindow &m_passwordPrompt;
};

#endif // __VIEWERVNCAUTHHANDLER_H__
