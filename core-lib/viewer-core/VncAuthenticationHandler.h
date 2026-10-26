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

#ifndef _VNC_AUTHENTICATION_HANDLER_H_
#define _VNC_AUTHENTICATION_HANDLER_H_

#include "viewer-core/AuthHandler.h"

class VncAuthenticationHandler : public AuthHandler
{
public:
  VncAuthenticationHandler();
  ~VncAuthenticationHandler() override;

  //
  // Overrides AuthHandler::authenticate(). The password is a request to the
  // driver (AwaitableReader::request()), which the driver serves by calling
  // getPassword(), outside the coroutines.
  //
  Task<> authenticate(AwaitableReader &reader, DataOutputStream *output) override;

  //
  // Overrides AuthHandler::addAuthCapability().
  //
  void addAuthCapability(CapabilitiesManager &capabilitiesManager) override;

protected:
  //
  // Gets the password: called by the driver, on the core thread, and it may
  // block, waiting for the user. Throws AuthCanceledException if the user
  // cancels.
  //
  virtual void getPassword(StringStorage *passString) = 0;

private:
  //
  // The request for the password, which the driver serves with
  // getPassword(); password holds the answer.
  //
  class PasswordRequest final : public AwaitableReader::Request
  {
  public:
    explicit PasswordRequest(VncAuthenticationHandler &handler)
    : m_handler(handler)
    {
    }

    void serve() override
    {
      m_handler.getPassword(&password);
    }

    StringStorage password;

  private:
    VncAuthenticationHandler &m_handler;
  };
};

#endif
