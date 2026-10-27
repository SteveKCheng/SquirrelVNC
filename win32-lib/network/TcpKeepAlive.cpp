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

#include "TcpKeepAlive.h"

#include "socket/SocketException.h"

void enableTcpKeepAlive(SOCKET socket, DWORD idleSeconds,
                        DWORD intervalSeconds, DWORD probeCount)
{
  auto set = [socket](int level, int name, const void *value, int length) {
    if (setsockopt(socket, level, name, static_cast<const char *>(value), length) == SOCKET_ERROR) {
      throw SocketException();
    }
  };
  BOOL enabled = TRUE;
  set(SOL_SOCKET, SO_KEEPALIVE, &enabled, sizeof(enabled));
  set(IPPROTO_TCP, TCP_KEEPIDLE, &idleSeconds, sizeof(idleSeconds));
  set(IPPROTO_TCP, TCP_KEEPINTVL, &intervalSeconds, sizeof(intervalSeconds));
  set(IPPROTO_TCP, TCP_KEEPCNT, &probeCount, sizeof(probeCount));
}
