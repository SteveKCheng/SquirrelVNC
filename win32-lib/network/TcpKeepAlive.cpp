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

#include "network/TcpKeepAlive.h"

#include "socket/SocketException.h"
#include "util/winhdr.h"

#include <cassert>
#include <system_error>

void enableTcpKeepAlive(asio::ip::tcp::socket &socket, int idleSeconds,
                        int intervalSeconds, int probeCount)
{
  assert(idleSeconds >= 0 && intervalSeconds >= 0 && probeCount >= 0);

  const SOCKET nativeSocket = socket.native_handle();
  auto set = [nativeSocket](int level, int name, const void *value, int length) {
    if (setsockopt(nativeSocket, level, name, static_cast<const char *>(value), length) == SOCKET_ERROR) {
      throw SocketException(std::error_code(WSAGetLastError(), std::system_category()));
    }
  };
  const BOOL enabled = TRUE;
  const DWORD idle = static_cast<DWORD>(idleSeconds);
  const DWORD interval = static_cast<DWORD>(intervalSeconds);
  const DWORD count = static_cast<DWORD>(probeCount);
  set(SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled));
  set(IPPROTO_TCP, TCP_KEEPIDLE, &idle, sizeof(idle));
  set(IPPROTO_TCP, TCP_KEEPINTVL, &interval, sizeof(interval));
  set(IPPROTO_TCP, TCP_KEEPCNT, &count, sizeof(count));
}
