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

#ifndef _TCP_ACCEPTOR_H_
#define _TCP_ACCEPTOR_H_

#include "log-writer/LogWriter.h"

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/ip/tcp.hpp>

#include <functional>
#include <memory>

//
// Accepts TCP connections on the network thread (NetworkThread), for
// listening mode, where servers connect to the viewer: each accepted
// connection is given to a handler that the creator supplies, which
// decides what becomes of it.
//
// The socket is opened, bound and set to listen by the constructor, on the
// caller's thread, so that a port in use is reported at once; accepting,
// and closing (stop()), happen on the executor's thread.
//
// Neither SO_REUSEADDR nor SO_EXCLUSIVEADDRUSE is set (Asio's acceptor
// constructor would set SO_REUSEADDR, which on Windows lets another socket
// bind the same port). With SO_EXCLUSIVEADDRUSE, binding the port again
// would fail while connections accepted earlier are still open.
//
// Threads: the constructor, stop() and the destructor are called on the
// owner's thread; the handler is called on the executor's thread. Not
// copyable or movable.
//
class TcpAcceptor final
{
public:
  //
  // Called on the executor's thread with each connection accepted, on the
  // executor given to the constructor. It must not throw (an exception is
  // logged, and the connection closed), and must not refer to anything its
  // creator may destroy before stop() has taken effect: see stop().
  //
  using AcceptHandler = std::function<void(asio::ip::tcp::socket)>;

  //
  // Opens a socket for endpoint's protocol, binds it to endpoint (port 0
  // picks a free port: see getPort()), sets it to listen, and starts
  // accepting on executor.
  //
  // Pre-conditions: onAccept is not empty; executor runs until stop() has
  // taken effect (the NetworkThread is joined after this object is
  // destroyed: its join would otherwise wait for the accept loop).
  // Post-conditions: connections are accepted until stop().
  // Throws SocketException, and leaves nothing open, if the socket
  // cannot be opened, bound or set to listen.
  //
  TcpAcceptor(asio::any_io_executor executor,
              const asio::ip::tcp::endpoint &endpoint,
              AcceptHandler onAccept,
              Logger *logger = nullptr);

  TcpAcceptor(const TcpAcceptor &) = delete;
  TcpAcceptor &operator=(const TcpAcceptor &) = delete;
  TcpAcceptor(TcpAcceptor &&) = delete;
  TcpAcceptor &operator=(TcpAcceptor &&) = delete;

  //
  // As stop(). Does not wait.
  //
  ~TcpAcceptor();

  //
  // The port bound: the one given to the constructor, or the one picked
  // for port 0.
  //
  unsigned short getPort() const noexcept;

  //
  // Stops accepting: posts the closing of the socket to the executor, and
  // returns without waiting for it. Once the closing has run, the handler
  // is not called again, not even for a connection that was accepted
  // before it but not yet handed over, and the accept loop ends, releasing
  // the handler. Until then, the handler may still be called. Calling it
  // again does nothing more.
  //
  // Post-conditions: the port is free soon after, once the closing has run
  // on the executor; binding it again at once may fail.
  //
  void stop();

private:
  struct State;

  //
  // Accepts until state->isStopped, giving each connection to
  // state->onAccept. Retries after RETRY_DELAY on an error other than a
  // connection given up by its client. Executor's thread.
  //
  static asio::awaitable<void> acceptLoop(std::shared_ptr<State> state);

  // Shared with the accept loop, which holds it until it ends.
  std::shared_ptr<State> m_state;
  unsigned short m_port;
  bool m_isStopped;
};

#endif // _TCP_ACCEPTOR_H_
