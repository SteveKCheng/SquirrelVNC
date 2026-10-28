// Copyright (C) 2011,2012 GlavSoft LLC.
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

#ifndef _CORE_EVENTS_ADAPTER_H_
#define _CORE_EVENTS_ADAPTER_H_

#include "io-lib/DataInputStream.h"
#include "io-lib/DataOutputStream.h"
#include "io-lib/IOException.h"
#include "rfb/FrameBuffer.h"
#include "region/Rect.h"
#include "util/Exception.h"

#include "AuthHandler.h"

#include <vector>

//
// What the protocol initialization established, for the application: given
// to CoreEventsAdapter::onConnected(), as a copy the application can keep,
// so that it does not read the core's state while the core runs.
//
struct ConnectionInfo
{
  // The desktop name from ServerInit.
  StringStorage desktopName;
  // The protocol version in use, such as "RFB 003.008\n" (with the newline
  // of the version message).
  StringStorage protocolString;
  // The codes of the client-to-server and server-to-client messages that
  // both sides support (TightVNC's capabilities).
  std::vector<UINT32> clientMsgCodes;
  std::vector<UINT32> serverMsgCodes;
};

//
// CoreEventsAdapter interface is used to pass events from RemoteViewerCore to
// your application.
//
// You should inherit this abstract class and override the methods you are
// interested in. For example, if all you need is to show the contents of the
// remote screen, then you should override two functions: onFrameBufferUpdate()
// and onFrameBufferPropChange().
//
// Threads: the events are called on the network thread, except
// onFrameBufferUpdate() and onFrameBufferPropChange(), which are called on
// its FbUpdateNotifier thread. An implementation must not wait for the GUI
// thread: no SendMessage(), and no calls on its windows that Windows turns
// into one (ShowWindow() and the like). The GUI thread may itself be waiting
// for the notifier thread to finish, in ~RemoteViewerCore(), and the network
// thread runs every connection. Post to the GUI thread instead.
//

class CoreEventsAdapter
{
public:
  //
  // Every event is pure virtual: an implementation says what it does with
  // each, if only nothing.
  //
  virtual ~CoreEventsAdapter() = default;

  //
  // Bell event has been received from the server.
  //
  virtual void onBell() = 0;

  //
  // New cut text (clipboard) contents has been received from the server.
  //
  virtual void onCutText(const StringStorage &cutText) = 0;

  //
  // Connection has been established.
  //
  virtual void onEstablished() = 0;

  //
  // Protocol has entered the normal interactive phase (in other words,
  // protocol initialization has been completed).
  //
  // output is the connection's output, for capabilities that send
  // their own messages, such as file transfer; it stays valid while the
  // core runs. info is what the initialization established.
  //
  virtual void onConnected(DataOutputStream *output, const ConnectionInfo &info) = 0;

  //
  // RemoteViewerCore has been disconnected by calling stop()
  // or connection with server is disconnected.
  //
  // FIXME: now, onDisconnect not called after onError().
  // FIXME: change documentation or call onDisconnect() after onError().
  virtual void onDisconnect(const StringStorage &message) = 0;

  //
  // Authentication has been failed.
  //
  virtual void onAuthError(const AuthException &exception) = 0;

  //
  // Error has been occured.
  //
  virtual void onError(const Exception &exception) = 0;

  // this event after update of frame buffer "fb" in rectangle "update".
  // guaranteed correct of frame buffer's area in rectangle "update".
  //
  // Frame buffer contents has been changed. During this callback,
  // the frame buffer is locked, and the rectangle is guaranteed to be valid
  // (no guarantees about other areas of the frame buffer).
  //
  virtual void onFrameBufferUpdate(const FrameBuffer &fb, const Rect &update) = 0;

  // changed properties of frame buffer.
  // In this moment frame buffer area is dirty and may be contained incorrect data
  //
  // Some properties of the frame buffer have been changed. Assume that new
  // frame buffer has been created and the old one has been destroyed. This
  // notification will be called on initial frame buffer allocation as well.
  //
  virtual void onFrameBufferPropChange(const FrameBuffer &fb) = 0;
};

#endif
