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

#ifndef _TCP_KEEP_ALIVE_H_
#define _TCP_KEEP_ALIVE_H_

#include "util/CommonHeader.h"

//
// Turns on TCP keepalive (SO_KEEPALIVE) on socket, so that a connection
// whose peer has gone without closing it (rebooted, unplugged, asleep)
// ends with an error instead of waiting for data forever: after
// idleSeconds without traffic, a probe is sent every intervalSeconds, and
// the connection is reset once probeCount probes in a row are unanswered.
// For an Asio socket, pass its native_handle().
//
// Pre-conditions: socket is an open TCP socket; each argument is at
// least 1.
// Throws SocketException if an option cannot be set. TCP_KEEPIDLE and
// TCP_KEEPINTVL need Windows 10 version 1709, TCP_KEEPCNT version 1703.
// The options set before the failing one stay set.
//
void enableTcpKeepAlive(SOCKET socket, DWORD idleSeconds,
                        DWORD intervalSeconds, DWORD probeCount);

#endif // _TCP_KEEP_ALIVE_H_
