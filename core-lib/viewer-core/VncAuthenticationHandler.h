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

#include <functional>
#include <optional>

class VncAuthenticationHandler : public AuthHandler
{
public:
  VncAuthenticationHandler();
  ~VncAuthenticationHandler() override;

  //
  // Overrides AuthHandler::authenticate(). The password is a request to the
  // driver (AwaitableReader::request()), which the driver serves by calling
  // requestPassword(), outside the coroutines, and waiting for its reply
  // without blocking. Throws AuthCanceledException if there is no password.
  //
  Task<> authenticate(AwaitableReader &reader, DataOutputStream *output) override;

  //
  // Called with the password, or with nothing if the user cancelled or the
  // request was cancelled.
  //
  using PasswordReply = std::function<void(std::optional<StringStorage>)>;

  //
  // Overrides AuthHandler::addAuthCapability().
  //
  void addAuthCapability(CapabilitiesManager &capabilitiesManager) override;

protected:
  //
  // Asks for the password, without waiting for it. Called by the driver, on
  // the network thread.
  //
  // Post-conditions: reply is called exactly once, on any thread: perhaps
  // before this returns, if the password is known; or once the user has
  // answered; or with nothing, if cancelPasswordRequest() is called first.
  //
  virtual void requestPassword(PasswordReply reply) = 0;

  //
  // Ends a pending requestPassword() early: its reply is called with nothing,
  // if it has not been called. On the network thread; does not wait.
  //
  virtual void cancelPasswordRequest() noexcept = 0;

private:
  //
  // The request for the password, which the driver serves with
  // requestPassword(); password holds the answer once it is done.
  //
  class PasswordRequest final : public AwaitableReader::Request
  {
  public:
    explicit PasswordRequest(VncAuthenticationHandler &handler)
    : m_handler(handler)
    {
    }

    void start(std::function<void(std::exception_ptr)> done) override
    {
      // The coroutine that owns this request waits until done is called.
      m_handler.requestPassword([this, done](std::optional<StringStorage> answer) {
        if (answer) {
          password = std::move(*answer);
          done(nullptr);
        } else {
          done(std::make_exception_ptr(AuthCanceledException()));
        }
      });
    }

    void cancel() noexcept override
    {
      m_handler.cancelPasswordRequest();
    }

    StringStorage password;

  private:
    VncAuthenticationHandler &m_handler;
  };
};

#endif
