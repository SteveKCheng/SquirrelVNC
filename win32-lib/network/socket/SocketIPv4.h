// Copyright (C) 2008,2009,2010,2011,2012 GlavSoft LLC.
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

#ifndef SOCKET_IPV4_H
#define SOCKET_IPV4_H

#include "sockdefs.h"

#include "SocketIPv4.h"
#include "SocketAddressIPv4.h"
#include "SocketException.h"

#include "io-lib/Channel.h"
#include "io-lib/IOException.h"
#include "win-system/WsaStartup.h"

#include <memory>

/**
 * IPv4 Socket class.
 *
 * @fixme refactor this class to avoid usage of SocketAddressIPv4 class.
 */
class SocketIPv4 final
{
public:
  /**
   * Creates new socket.
   */
  SocketIPv4();
  SocketIPv4(const SocketIPv4 &) = delete;
  SocketIPv4 &operator=(const SocketIPv4 &) = delete;
  SocketIPv4(SocketIPv4 &&) = delete;
  SocketIPv4 &operator=(SocketIPv4 &&) = delete;

  /**
   * Deletes and closes socket.
   */
  ~SocketIPv4();

  /**
   * Connects to remote host.
   * @param host host to connect.
   * @param port port to connect.
   * @throws SocketException on fail.
   */
  void connect(const TCHAR *host, unsigned short port);
  /**
   * Connects to remote host.
   * @param addr address to connect.
   * @throws SocketException on fail.
   */
  void connect(const SocketAddressIPv4 &addr);
  /**
   * Closes socket.  Does not throw.
   */
  void close();
  /**
   * Shutdowns socket.
   * @param how how to shutdown socket (SD_RECEIVE|SD_SEND|SD_BOTH).
   * @throws SocketException on fail.
   */
  void shutdown(int how);

  //
  // Hands the socket over to the caller, who must close it, for another
  // owner such as an Asio socket (RemoteViewerCore, for a connection
  // accepted in listening mode).
  //
  // Post-conditions: returns the socket; this object has no socket any
  // more, so its destructor closes nothing, and shutdown() and the other
  // operations fail.
  //
  SOCKET release() noexcept;
  /**
   * Binds socket to specified address.
   * @param bindHost host to bind.
   * @param bindPort port to bind.
   * @throws SocketException on fail.
   */
  void bind(const TCHAR *bindHost, unsigned short bindPort);
  /**
   * Binds socket to socket address.
   * @throws SocketException on fail.
   */
  void bind(const SocketAddressIPv4 &addr);
  /**
   * Socket listen method.
   * @param backlog max count of connections in pool.
   * @throws SocketException on fail.
   */
  void listen(int backlog);

  /**
   * Accepts incoming connection.
   * @throws SocketException on fail.
   * @return newly allocated socket that contain incoming connections.
   */
  std::unique_ptr<SocketIPv4> accept();

  /**
   * Sends data to socket.
   *
   * @param data buffer to send.
   * @param size bytes to send.
   * @param [optional] flags socket flags.
   * @return count to sent bytes.
   * @throw IOException on error.
   */
  int send(const char *data, int size, int flags = 0);
  /**
   * Receives data from socket.
   *
   * @param buffer buffer to receive data.
   * @param size count of bytes to read from socket.
   * @param flags recv flags.
   * @return count of read bytes.
   * @throws IOException on fail.
   */
  int recv(char *buffer, int size, int flags = 0);

  int available();

  /* Auxiliary */
  void setSocketOptions(int level, int name, void *value, socklen_t len);

  /* Socket options */
  void enableNaggleAlgorithm(bool enabled);
  void setExclusiveAddrUse();

  //
  // Turns on TCP keepalive (SO_KEEPALIVE), so that a connection whose peer
  // has gone without closing it (rebooted, unplugged, asleep) ends with an
  // error instead of waiting for data forever: after idleSeconds without
  // traffic, a probe is sent every intervalSeconds, and the connection is
  // reset once probeCount probes in a row are unanswered.
  //
  // Pre-conditions: the socket is a TCP socket; each argument is at least 1.
  // Throws SocketException if an option cannot be set. TCP_KEEPIDLE and
  // TCP_KEEPINTVL need Windows 10 version 1709, TCP_KEEPCNT version 1703.
  // The options set before the failing one stay set.
  //
  void enableKeepAlive(DWORD idleSeconds, DWORD intervalSeconds, DWORD probeCount);

  //
  // As above, for a socket that no SocketIPv4 holds (an Asio socket's).
  //
  static void enableKeepAlive(SOCKET socket, DWORD idleSeconds,
                              DWORD intervalSeconds, DWORD probeCount);

private:
  WsaStartup m_wsaStartup;

protected:
  // Returns a SOCKET object with performed accept operation.
  // Throws SocketException on an error.
  SOCKET getAcceptedSocket(struct sockaddr_in *addr);

  // Closes old socket and sets handler to new one
  //
  // Post-conditions: this object owns socket. Cannot fail.
  void set(SOCKET socket);

  /**
   * WinSock socket.
   */
  SOCKET m_socket;
  bool m_isClosed;

  /**
   * Flag determinating if socket is server or client socket.
   */
  bool m_isBound;
};

#endif
