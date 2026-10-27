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

#include "LoopbackServer.h"

#include "network/socket/SocketException.h"

#include <asio/as_tuple.hpp>
#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

LoopbackServer::LoopbackServer(std::vector<char> serverBytes)
: m_serverBytes(std::move(serverBytes)),
  m_acceptor(m_context),
  m_port(0)
{
  asio::ip::tcp::endpoint endpoint(asio::ip::address_v4::loopback(), 0);
  std::error_code error;
  m_acceptor.open(endpoint.protocol(), error);
  if (!error) {
    m_acceptor.bind(endpoint, error);
  }
  if (!error) {
    m_acceptor.listen(1, error);
  }
  if (!error) {
    m_port = m_acceptor.local_endpoint(error).port();
  }
  if (error) {
    throw SocketException(error.value());
  }
  asio::co_spawn(m_context, serve(), asio::detached);
  m_thread = std::thread([this] { m_context.run(); });
}

LoopbackServer::~LoopbackServer()
{
  m_context.stop();
  if (m_thread.joinable()) {
    m_thread.join();
  }
}

UINT16 LoopbackServer::getPort() const
{
  return m_port;
}

std::vector<char> LoopbackServer::finish()
{
  m_thread.join();
  return std::move(m_received);
}

asio::awaitable<void> LoopbackServer::serve()
{
  auto [acceptError, client] =
    co_await m_acceptor.async_accept(asio::as_tuple(asio::use_awaitable));
  if (acceptError) {
    co_return;
  }
  std::error_code ignored;
  m_acceptor.close(ignored);
  co_await asio::async_write(client, asio::buffer(m_serverBytes),
                             asio::as_tuple(asio::use_awaitable));
  client.shutdown(asio::ip::tcp::socket::shutdown_send, ignored);
  char buffer[4096];
  for (;;) {
    auto [readError, count] =
      co_await client.async_read_some(asio::buffer(buffer), asio::as_tuple(asio::use_awaitable));
    m_received.insert(m_received.end(), buffer, buffer + count);
    if (readError) {
      break;
    }
  }
}
