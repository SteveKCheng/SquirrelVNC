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

#include "TcpAcceptor.h"

#include "network/socket/SocketException.h"

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/error.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>

#include <cassert>
#include <chrono>
#include <utility>

namespace
{
  // Connections the system queues before they are accepted, as before
  // (TcpServer).
  const int BACKLOG = 10;

  // The wait before accepting again after an error that is not a client
  // giving up, which may be a shortage that passes (of memory, of handles).
  const std::chrono::seconds RETRY_DELAY(1);
}

struct TcpAcceptor::State
{
  State(asio::any_io_executor executor, AcceptHandler handler, Logger *logger)
  : acceptor(executor),
    retryTimer(executor),
    onAccept(std::move(handler)),
    logWriter(logger),
    isStopped(false)
  {
  }

  asio::ip::tcp::acceptor acceptor;
  asio::steady_timer retryTimer;
  AcceptHandler onAccept;
  LogWriter logWriter;
  // Set by the closing that stop() posts; executor's thread only, after
  // the constructor.
  bool isStopped;
};

TcpAcceptor::TcpAcceptor(asio::any_io_executor executor,
                         const asio::ip::tcp::endpoint &endpoint,
                         AcceptHandler onAccept,
                         Logger *logger)
: m_state(std::make_shared<State>(executor, std::move(onAccept), logger)),
  m_port(0),
  m_isStopped(false)
{
  assert(m_state->onAccept);
  asio::ip::tcp::acceptor &acceptor = m_state->acceptor;
  std::error_code error;
  acceptor.open(endpoint.protocol(), error);
  if (!error) {
    acceptor.bind(endpoint, error);
  }
  if (!error) {
    acceptor.listen(BACKLOG, error);
  }
  if (!error) {
    m_port = acceptor.local_endpoint(error).port();
  }
  if (error) {
    // The socket, if opened, is closed with m_state: nothing is pending.
    throw SocketException(error.value());
  }
  m_state->logWriter.info(_T("Listening on %hs:%hu"),
                          endpoint.address().to_string().c_str(), m_port);
  asio::co_spawn(executor, acceptLoop(m_state), asio::detached);
}

TcpAcceptor::~TcpAcceptor()
{
  stop();
}

unsigned short TcpAcceptor::getPort() const noexcept
{
  return m_port;
}

void TcpAcceptor::stop()
{
  if (m_isStopped) {
    return;
  }
  m_isStopped = true;
  asio::post(m_state->acceptor.get_executor(), [state = m_state] {
    state->isStopped = true;
    std::error_code ignored;
    state->acceptor.close(ignored);
    state->retryTimer.cancel();
  });
}

asio::awaitable<void> TcpAcceptor::acceptLoop(std::shared_ptr<State> state)
{
  while (!state->isStopped) {
    auto [error, socket] =
      co_await state->acceptor.async_accept(asio::as_tuple(asio::use_awaitable));
    // Also for a connection accepted before the closing, but resumed
    // after it: the socket is closed here.
    if (state->isStopped) {
      break;
    }
    if (!error) {
      std::error_code ignored;
      asio::ip::tcp::endpoint peer = socket.remote_endpoint(ignored);
      state->logWriter.info(_T("Accepted a connection from %hs:%hu"),
                            peer.address().to_string().c_str(), peer.port());
      try {
        state->onAccept(std::move(socket));
      } catch (...) {
        state->logWriter.error(_T("Cannot take the connection accepted; it is closed"));
      }
    } else if (error == asio::error::connection_aborted ||
               error == asio::error::connection_reset) {
      // The client gave up before the connection was accepted.
      state->logWriter.detail(_T("A connection was closed before it was accepted"));
    } else {
      state->logWriter.error(_T("Cannot accept a connection (%hs); trying again"),
                             error.message().c_str());
      state->retryTimer.expires_after(RETRY_DELAY);
      co_await state->retryTimer.async_wait(asio::as_tuple(asio::use_awaitable));
    }
  }
}
