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

#ifndef _MULTI_THREAD_TCP_SERVER_H_
#define _MULTI_THREAD_TCP_SERVER_H_

#include "thread/Thread.h"
#include "util/Exception.h"
#include "network/socket/SocketIPv4.h"

/**
 * Abstract multithreaded TCP server class.
 * Bind on specified host and port and listening for connections,
 * but don't know what to do with incoming connections.
 * @usage create subclass of TcpServer and call start (or use autoStart flag)
 * in TcpServer constructor.
 */
class TcpServer : private Thread
{
public:
  /**
   * Creates new TcpServer, bound and listening, whose thread does not
   * accept connections until the subclass calls start(). (It cannot
   * start here: the thread calls onAcceptConnection(), of a subclass
   * that is not constructed yet.)
   * @param bindHost host to bind.
   * @param bindPort port to bind.
   * @param lockAddr determinates if need to lock adress to other processes cannot reuse it.
   * @throws Exception if fail to create tcp server.
   */
  TcpServer(const TCHAR *bindHost,
            unsigned short bindPort,
            bool lockAddr = false);
  /**
   * Closes listening socket, terminates tcp server thread and
   * deletes tcp server object.
   *
   * Pre-conditions: the subclass's destructor has called stop().
   */
  virtual ~TcpServer();

  /**
   * Returns bind port.
   */
  unsigned short getBindPort() const;

protected:
  /**
   * Starts tcp server thread (listen for incoming connections).
   * Pre-conditions: the subclass is constructed; called once.
   */
  virtual void start();

  /**
   * Closes the listening socket and waits for the thread to end, if it
   * was started (one that was not never runs). A subclass calls it first
   * in its destructor,
   * before what onAcceptConnection() uses is destroyed.
   * Post-conditions: onAcceptConnection() is not running, and is not
   * called again. Calling it again does nothing more.
   */
  void stop();

  /**
   * Called from tcp server thread when server accepts connection to process it.
   * @param socket incoming connection socket, not empty, given to the
   * subclass.
   */
  virtual void onAcceptConnection(std::unique_ptr<SocketIPv4> socket) = 0;

  /**
   * Inherited from Thread class.
   * Listening for incoming tcp connections.
   */
  virtual void execute();

private:
  /**
   * Listening socket.
   */
  SocketIPv4 m_listenSocket;
  /**
   * Host to bind.
   */
  StringStorage m_bindHost;
  /**
   * Port to bind.
   */
  unsigned short m_bindPort;
};

#endif
