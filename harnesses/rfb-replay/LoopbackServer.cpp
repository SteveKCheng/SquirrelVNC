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

#include <algorithm>
#include <climits>

LoopbackServer::LoopbackServer(std::vector<char> serverBytes)
: m_wsaStartup(1, 2),
  m_serverBytes(std::move(serverBytes)),
  m_listener(INVALID_SOCKET),
  m_client(INVALID_SOCKET),
  m_port(0)
{
  m_listener = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (m_listener == INVALID_SOCKET) {
    throw SocketException();
  }
  sockaddr_in address = {};
  address.sin_family = AF_INET;
  address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
  address.sin_port = 0;
  int length = sizeof(address);
  if (bind(m_listener, reinterpret_cast<sockaddr *>(&address), sizeof(address)) == SOCKET_ERROR ||
      listen(m_listener, 1) == SOCKET_ERROR ||
      getsockname(m_listener, reinterpret_cast<sockaddr *>(&address), &length) == SOCKET_ERROR) {
    SocketException error;
    closesocket(m_listener);
    throw error;
  }
  m_port = ntohs(address.sin_port);
  m_thread = std::thread([this] { serve(); });
}

LoopbackServer::~LoopbackServer()
{
  closesocket(m_listener);
  SOCKET client = m_client.exchange(INVALID_SOCKET);
  if (client != INVALID_SOCKET) {
    closesocket(client);
  }
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

void LoopbackServer::serve()
{
  SOCKET client = accept(m_listener, nullptr, nullptr);
  if (client == INVALID_SOCKET) {
    return;
  }
  m_client = client;
  size_t sent = 0;
  while (sent < m_serverBytes.size()) {
    size_t chunk = std::min<size_t>(m_serverBytes.size() - sent, INT_MAX);
    int count = send(client, m_serverBytes.data() + sent, static_cast<int>(chunk), 0);
    if (count == SOCKET_ERROR) {
      break;
    }
    sent += static_cast<size_t>(count);
  }
  shutdown(client, SD_SEND);
  char buffer[4096];
  for (;;) {
    int count = recv(client, buffer, sizeof(buffer), 0);
    if (count <= 0) {
      break;
    }
    m_received.insert(m_received.end(), buffer, buffer + count);
  }
  client = m_client.exchange(INVALID_SOCKET);
  if (client != INVALID_SOCKET) {
    closesocket(client);
  }
}
