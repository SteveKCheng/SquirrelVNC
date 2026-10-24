// Copyright (C) 2009,2010,2011,2012 GlavSoft LLC.
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

#include "TcpServer.h"

#include "network/socket/SocketAddressIPv4.h"

TcpServer::TcpServer(const TCHAR *bindHost, unsigned short bindPort,
                     bool lockAddr)
: m_bindHost(bindHost), m_bindPort(bindPort)
{
  SocketAddressIPv4 bindAddr = SocketAddressIPv4::resolve(bindHost, bindPort);

  if (lockAddr) {
    m_listenSocket.setExclusiveAddrUse();
  }

  m_listenSocket.bind(bindAddr);
  m_listenSocket.listen(10);
}

TcpServer::~TcpServer()
{
  stop();
}

void TcpServer::stop()
{
  try { m_listenSocket.shutdown(SD_BOTH); } catch(...) { }
  try { m_listenSocket.close(); } catch (...) { }

  if (isActive()) {
    Thread::terminate();
    Thread::wait();
  }
}

unsigned short TcpServer::getBindPort() const
{
  return m_bindPort;
}

void TcpServer::start()
{
  resume();
}

void TcpServer::execute()
{
  while (!isTerminating()) {
    std::unique_ptr<SocketIPv4> clientSocket;

    try {
      clientSocket = m_listenSocket.accept();
    } catch (...) {
    }

    if (clientSocket) {
      onAcceptConnection(std::move(clientSocket));
    } else {
      break ;
    }
  }
}
