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

#ifndef SOCKET_STREAM_H
#define SOCKET_STREAM_H

#include "io-lib/Channel.h"

#include "../socket/SocketAddressIPv4.h"
#include "../socket/SocketIPv4.h"
#include "../socket/SocketAddressIPv4.h"

#include "io-lib/IOException.h"

class SocketStream : public Channel
{
public:
  SocketStream(SocketIPv4 *);
  virtual ~SocketStream();

  //
  // Inherited from Channel.
  //

  // read() and write() throw IOException on failure, including a closed
  // connection in read().
  virtual size_t read(void *, size_t);

  virtual size_t write(const void *, size_t);

  // Closes connection and break all blocked operation.
  // Does not throw: a failure to shut the connection down is ignored.
  virtual void close();

  virtual size_t available();

protected:
  SocketIPv4 *m_socket;

  friend class SocketIPv4;
};

#endif
