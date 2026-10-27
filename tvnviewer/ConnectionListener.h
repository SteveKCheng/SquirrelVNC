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

#ifndef __CONNECTION_LISTENER_H__
#define __CONNECTION_LISTENER_H__

#include "viewer-core/TcpAcceptor.h"
#include "win-system/WindowsApplication.h"

#include <asio/any_io_executor.hpp>
#include <asio/ip/tcp.hpp>

#include <deque>
#include <memory>
#include <mutex>
#include <optional>

//
// Listening mode: accepts connections from servers on a port, on every
// IPv4 address, on the network thread (TcpAcceptor), and hands them to the
// GUI thread. Each connection is queued here, in the order accepted, and
// TvnViewer::WM_USER_NEW_LISTENING is posted to the application, whose
// handler takes them (getNewConnection()).
//
// Threads: GUI thread; only the queueing runs on the network thread.
//
class ConnectionListener final
{
public:
  static const UINT16 DEFAULT_PORT = 5500;

  //
  // Starts listening on port.
  //
  // Pre-conditions: application's main window exists (WindowsApplication::
  // run() creates it), and application stays valid until the network
  // thread has run this object's stopping (TcpAcceptor::stop()): it owns
  // the NetworkThread of executor, which is joined after this object is
  // destroyed (TvnViewer).
  // Throws SocketException if the port cannot be bound.
  //
  ConnectionListener(WindowsApplication *application,
                     asio::any_io_executor executor,
                     UINT16 port);

  //
  // Stops listening, without waiting (TcpAcceptor::stop()). The
  // connections not taken are closed once the network thread has let go
  // of the queue.
  //
  ~ConnectionListener() = default;

  ConnectionListener(const ConnectionListener &) = delete;
  ConnectionListener &operator=(const ConnectionListener &) = delete;

  //
  // Takes the oldest connection accepted and not yet taken, or returns
  // nothing if there is none.
  // Post-conditions: the connection returned is the caller's; its socket
  // belongs to the network thread's executor.
  //
  std::optional<asio::ip::tcp::socket> getNewConnection();

  UINT16 getBindPort() const;

private:
  // Shared with the acceptor's handler, which may queue into it until the
  // stopping has run.
  struct Inbox
  {
    std::mutex lock;
    std::deque<asio::ip::tcp::socket> connections;
  };

  std::shared_ptr<Inbox> m_inbox;
  TcpAcceptor m_acceptor;
};

#endif
