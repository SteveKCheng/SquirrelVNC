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
  // connectionData. Both must outlive the handler's use by the core thread.
  //
  ViewerVncAuthHandler(ConnectionData *connectionData,
                       ViewerWindow &passwordPrompt);
  ~ViewerVncAuthHandler() override;

private:
  //
  // Called on the core thread. Waits for the user if the password is not
  // known yet (ViewerWindow::askPassword()).
  //
  // Post-conditions: passString and m_connectionData hold the password.
  // Throws AuthCanceledException if the user cancelled, or the request was
  // cancelled (ViewerWindow::cancelPasswordRequest()).
  //
  void getPassword(StringStorage *passString) override;

  ConnectionData *m_connectionData;
  ViewerWindow &m_passwordPrompt;
};

#endif // __VIEWERVNCAUTHHANDLER_H__
