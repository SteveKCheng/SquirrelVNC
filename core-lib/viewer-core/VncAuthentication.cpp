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

#include "VncAuthentication.h"

#include "util/DesCrypt.h"
#include "util/TextEncoding.h"

#include <algorithm>

Task<> VncAuthentication::vncAuthenticate(AwaitableReader &reader,
                                          DataOutputStream *output,
                                          const StringStorage &password)
{
  // TODO: removed duplicate code: ControlAuth.cpp

  // Prepare data for authentication: the key is the first 8 bytes of the
  // password in UTF-8, as TigerVNC's vncpasswd stores it, padded with
  // zeros. Cutting may split a character; the key is only bytes.
  std::vector<std::byte> passwordUtf8 = TextEncoding::toUtf8(password);

  UINT8 m_password[VNC_PASSWORD_SIZE];
  memset(m_password, 0, sizeof(m_password));
  memcpy(m_password, passwordUtf8.data(),
         std::min(passwordUtf8.size(), sizeof(m_password)));

  UINT8 challenge[16];
  UINT8 response[16];

  co_await reader.readFully(challenge, sizeof(challenge));
  DesCrypt desCrypt;
  desCrypt.encrypt(response, challenge, sizeof(challenge), m_password);
  output->writeFully(response, sizeof(response));
  output->flush();
}
