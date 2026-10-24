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

#ifndef _TCP_CONNECTION_H_
#define _TCP_CONNECTION_H_

#include "log-writer/LogWriter.h"
#include "network/RfbInputGate.h"
#include "network/RfbOutputGate.h"
#include "network/socket/SocketIPv4.h"
#include "network/socket/SocketStream.h"
#include "thread/LocalMutex.h"
#include "io-lib/BufferedInputStream.h"

#include <memory>

class TcpConnection
{
public:
  TcpConnection(LogWriter *logWriter);
  TcpConnection(const TcpConnection &) = delete;
  TcpConnection &operator=(const TcpConnection &) = delete;
  TcpConnection(TcpConnection &&) = delete;
  TcpConnection &operator=(TcpConnection &&) = delete;

  virtual ~TcpConnection();

  void bind(const TCHAR *host, UINT16 port);
  void bind(SocketIPv4 *socket);
  void bind(RfbInputGate *input, RfbOutputGate *output);

  void connect();
  void close();

  RfbInputGate *getInput() const;
  RfbOutputGate *getOutput() const;
private:
  StringStorage m_host;
  UINT16 m_port;
  // The socket and gates in use: those given to bind(), or those that
  // connect() made, which are owned by the members below.
  SocketIPv4 *m_socket;
  RfbInputGate *m_input;
  RfbOutputGate *m_output;

  // Declared in the order of construction, so that each is destroyed
  // before the one it uses.
  std::unique_ptr<SocketIPv4> m_ownSocket;
  std::unique_ptr<SocketStream> m_socketStream;
  std::unique_ptr<BufferedInputStream> m_bufInput;
  std::unique_ptr<RfbInputGate> m_ownInput;
  std::unique_ptr<RfbOutputGate> m_ownOutput;

  bool m_wasBound;
  bool m_wasConnected;
  bool m_isEstablished;

  LogWriter *m_logWriter;

  mutable LocalMutex m_connectLock;
};

#endif
