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

#ifndef _LOOPBACK_SERVER_H_
#define _LOOPBACK_SERVER_H_

#include "util/winhdr.h"
#include "win-system/WsaStartup.h"

#include <atomic>
#include <thread>
#include <vector>

//
// A server for one client over TCP on 127.0.0.1, so that rfb-replay --tcp
// can drive RemoteViewerCore through a real socket, as the viewer does:
// resolving, connecting, and reading and writing on the network thread.
//
// On a thread of its own, it accepts one client, sends it the bytes given,
// shuts down its sending side (so the client reads the end of the stream),
// and collects what the client sends until the client closes the
// connection.
//
// Not copyable or movable.
//
class LoopbackServer final
{
public:
  //
  // Listens on 127.0.0.1, on a port the system chooses (getPort()), and
  // starts serving serverBytes.
  //
  // Throws SocketException if the socket cannot be set up.
  //
  explicit LoopbackServer(std::vector<char> serverBytes);

  LoopbackServer(const LoopbackServer &) = delete;
  LoopbackServer &operator=(const LoopbackServer &) = delete;

  //
  // Closes the sockets, which ends the thread if it still waits, and waits
  // for it.
  //
  ~LoopbackServer();

  UINT16 getPort() const;

  //
  // Waits until the client has closed the connection, and returns what it
  // sent, in order.
  //
  // Pre-conditions: called once; the client connects and, at some point,
  // closes the connection.
  //
  std::vector<char> finish();

private:
  void serve();

  WsaStartup m_wsaStartup;
  std::vector<char> m_serverBytes;
  std::vector<char> m_received;
  SOCKET m_listener;
  std::atomic<SOCKET> m_client;
  UINT16 m_port;
  std::thread m_thread;
};

#endif // _LOOPBACK_SERVER_H_
