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

#ifndef _CAPABILITIES_MANAGER_H_
#define _CAPABILITIES_MANAGER_H_

#include "util/CommonHeader.h"
#include "util/inttypes.h"

#include <functional>
#include <memory>

class AuthHandler;
class ServerMessageListener;
class Decoder;

//
// What the core offers the modules that extend it (authentication
// handlers, the file transfer): registering their capabilities, and
// running work on the network thread with the core kept alive. Given out
// by RemoteViewerCore::getCapabilitiesManager().
//
// Threads: the add...() functions are for the core's consumer thread,
// before RemoteViewerCore::start(); postToLoop() and claimOwner() are for
// any thread, at any time.
//
class CapabilitiesManager
{
public:
  CapabilitiesManager();
  virtual ~CapabilitiesManager();

  //
  // This function add authentication capability and authentication handler.
  //
  virtual void addAuthCapability(AuthHandler *authHandler,
                                 UINT32 code,
                                 const char *vendorSignature,
                                 const char *nameSignature,
                                 const StringStorage description = _T("")) = 0;

  //
  // This function add capability and handler, to process server-to-client messages.
  //
  virtual void addServerMsgCapability(ServerMessageListener *listener,
                                      UINT32 code,
                                      const char *vendorSignature,
                                      const char *nameSignature,
                                      const StringStorage description = _T("")) = 0;

  //
  // This function add capability, to process client-to-server messages.
  //
  virtual void addClientMsgCapability(UINT32 code,
                                      const char *vendorSignature,
                                      const char *nameSignature,
                                      const StringStorage description = _T("")) = 0;

  //
  // This function add encoding capability and decoder handler.
  // Parameter "priorityEncoding" define priority of encoding.
  // Encoding send to order in decrease of priority.
  // The decoder is owned from then on, and goes to the decoder store
  // if the server enables the encoding.
  //
  virtual void addEncodingCapability(std::unique_ptr<Decoder> decoder,
                                     int priorityEncoding,
                                     UINT32 code,
                                     const char *vendorSignature,
                                     const char *nameSignature,
                                     const StringStorage description = _T("")) = 0;

  //
  // Posts work for the core, or for what is registered with it (such as
  // the file transfer), to the network thread, with a reference to the
  // adapter given to RemoteViewerCore::start(), which keeps the adapter's
  // owner, and so the core, alive until the work has run (see start()).
  // Does nothing once no reference is left: the owner is being deleted,
  // and the connection has ended; or start() has not been called yet.
  // Work posted after the connection has ended still runs, while the owner
  // is alive.
  //
  // Pre-conditions: work does not throw.
  //
  virtual void postToLoop(std::function<void()> work) = 0;

  //
  // A claim on the adapter's owner, as postToLoop() takes for its work:
  // for work that runs elsewhere and needs the core, and what is
  // registered with it, alive until it ends. Empty once no reference is
  // left, or before start() (see postToLoop()).
  //
  virtual std::shared_ptr<void> claimOwner() = 0;
};

#endif
