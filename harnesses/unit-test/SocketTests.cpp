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

//
// Tests of the socket options set on connections (network/TcpKeepAlive.h).
//

#include "UnitTest.h"

#include "network/TcpKeepAlive.h"
#include "network/socket/SocketException.h"

#include <asio/io_context.hpp>
#include <asio/ip/tcp.hpp>

void runSocketTests()
{
  bool ok = true;
  try {
    asio::io_context context;
    asio::ip::tcp::socket socket(context, asio::ip::tcp::v4());
    enableTcpKeepAlive(socket.native_handle(), 30, 5, 10);
  } catch (const SocketException &) {
    ok = false;
  }
  check(ok, "enableTcpKeepAlive sets its options on a TCP socket");
}
