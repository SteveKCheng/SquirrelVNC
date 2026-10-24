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

#include "thread/AutoLock.h"

#include "TcpConnection.h"

TcpConnection::TcpConnection(LogWriter *logWriter)
: m_logWriter(logWriter)
{
  m_port = 0;
  m_socket = 0;
  m_input = 0;
  m_output = 0;
  m_wasBound = false;
  m_wasConnected = false;
  m_isEstablished = false;
}

void TcpConnection::bind(const TCHAR *host, UINT16 port)
{
  AutoLock al(&m_connectLock);
  if (m_wasBound) {
    throw Exception(_T("Tcp-connection already bound"));
  }
  m_host = host;
  m_port = port;
  m_socket = 0;
  m_input = 0;
  m_output = 0;
  m_wasBound = true;
}

void TcpConnection::bind(SocketIPv4 *socket)
{
  AutoLock al(&m_connectLock);
  if (m_wasBound) {
    throw Exception(_T("Tcp-connection already bound"));
  }
  m_host = _T("");
  m_port = 0;
  m_socket = socket;
  m_input = 0;
  m_output = 0;
  m_wasBound = true;
}

void TcpConnection::bind(RfbInputGate *input, RfbOutputGate *output)
{
  AutoLock al(&m_connectLock);
  if (m_wasBound) {
    throw Exception(_T("Tcp-connection already bound"));
  }
  m_host = _T("");
  m_port = 0;
  m_socket = 0;
  m_input = input;
  m_output = output;
  m_wasBound = true;
}

void TcpConnection::connect()
{
  // if connection is already established, then method do nothing.
  {
    AutoLock al(&m_connectLock);
    m_wasConnected = true;
  }
  // need create to gates
  if (m_input == 0 && m_output == 0) {
    // need create to socket
    if (m_socket == 0) {
      if (!m_host.isEmpty() && m_port != 0) {
        SocketAddressIPv4 ipAddress(m_host.getString(), m_port);

        StringStorage ipAddressString;
        ipAddress.toString(&ipAddressString);
        m_logWriter->detail(_T("Connecting to the host \"%s:%hu\" (%s:%hu)..."),
                            m_host.getString(), m_port,
                            ipAddressString.getString(), m_port);

        m_ownSocket = std::make_unique<SocketIPv4>();
        m_socket = m_ownSocket.get();
        m_socket->connect(ipAddress);
        m_socket->enableNaggleAlgorithm(false);
      } else {
        throw Exception(_T("Connection parameters (host, port, socket, gates) is empty."));
      }
    }

    m_logWriter->detail(_T("Initialization of socket stream and input/output gates..."));
    m_socketStream = std::make_unique<SocketStream>(m_socket);
    m_bufInput = std::make_unique<BufferedInputStream>(m_socketStream.get());
    m_ownInput = std::make_unique<RfbInputGate>(m_bufInput.get());
    m_ownOutput = std::make_unique<RfbOutputGate>(m_socketStream.get());
    m_input = m_ownInput.get();
    m_output = m_ownOutput.get();
  } else {
    _ASSERT(m_input != 0 && m_output != 0);
  }
  {
    AutoLock al(&m_connectLock);
    m_isEstablished = true;
  }
}

void TcpConnection::close()
{
  if (!m_host.isEmpty() && m_port != 0) {
    if (m_socketStream) {
      m_socketStream->close();
    }
  }
}

RfbInputGate *TcpConnection::getInput() const
{
  {
    AutoLock al(&m_connectLock);
    if (!m_isEstablished) {
      throw Exception(_T("Connecting has not been established"));
    }
  }
  return m_input;
}

RfbOutputGate *TcpConnection::getOutput() const
{
  {
    AutoLock al(&m_connectLock);
    if (!m_isEstablished) {
      throw Exception(_T("Connection has not been established"));
    }
  }
  return m_output;
}

TcpConnection::~TcpConnection()
{
  // What connect() made is freed by the unique_ptrs, gates first.
}
