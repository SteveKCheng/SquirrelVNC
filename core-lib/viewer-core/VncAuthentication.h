// Copyright (C) 2011,2012 GlavSoft LLC.
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

#ifndef _VNC_AUTHENTICATION_H_
#define _VNC_AUTHENTICATION_H_

#include "AuthHandler.h"

class VncAuthentication
{
public:
  //
  // Answers the server's challenge in VNC authentication.
  //
  // Pre-conditions: password is not null; the security type has been
  // chosen.
  // Post-conditions: the 16-byte challenge has been read, and the response
  // written and flushed: the challenge encrypted with DES, keyed with the
  // first 8 bytes of the password in UTF-8, padded with zeros.
  // Throws whatever reading or writing throws.
  //
  static void vncAuthenticate(DataInputStream *input, DataOutputStream *output,
                    const StringStorage *password);

  // TODO: removed duplicate code: ServerConfig.h
  static const int VNC_PASSWORD_SIZE = 8;
};

#endif
