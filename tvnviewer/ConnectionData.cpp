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

#include <algorithm>
#include <cstring>
#include <iomanip>
#include <sstream>

#include "ConnectionData.h"
#include "util/Exception.h"
#include "util/TextEncoding.h"
#include "util/VncPassCrypt.h"
#include "viewer-core/VncAuthentication.h"

//
// Post-conditions: returns text from HostPath, which holds UTF-8, or an
// empty string for null (as HostPath returns when it holds no path).
//
static StringStorage fromHostPath(const char *text)
{
  if (text == 0) {
    return StringStorage();
  }
  return TextEncoding::fromUtf8(std::as_bytes(std::span(text, strlen(text))));
}

ConnectionData::ConnectionData()
: m_isEmpty(true),
  m_isSetPassword(false),
  m_isIncoming(false),
  m_hasPlainPassword(false)
{
}

ConnectionData::ConnectionData(const ConnectionData &connectionData)
: m_isEmpty(connectionData.m_isEmpty),
  m_isSetPassword(connectionData.m_isSetPassword),
  m_isIncoming(connectionData.m_isIncoming),
  m_hasPlainPassword(false)
{
  if (!connectionData.isEmpty()) {
    m_hostPath.set(connectionData.m_hostPath.get());
  }
  if (m_isSetPassword) {
    m_defaultPassword = connectionData.m_defaultPassword;
    m_plainPassword = connectionData.m_plainPassword;
    m_hasPlainPassword = connectionData.m_hasPlainPassword;
  }
}

void ConnectionData::setIncoming(bool isIncoming)
{
  m_isIncoming = isIncoming;
}

bool ConnectionData::isIncoming() const
{
  return m_isIncoming;
}

bool ConnectionData::isEmpty() const
{
  return m_isEmpty;
}

bool ConnectionData::isSetPassword() const
{
  return m_isSetPassword;
}

void ConnectionData::resetPassword()
{
  m_isSetPassword = false;
  m_hasPlainPassword = false;
  m_plainPassword.setString(_T(""));
}

void ConnectionData::setHost(const StringStorage *host)
{
  StringStorage chompedString = *host;
  TCHAR spaceChar[] = _T(" \t\n\r");
  chompedString.removeChars(spaceChar, sizeof(spaceChar)/sizeof(TCHAR));

  std::vector<std::byte> hostUtf8 = TextEncoding::toUtf8(chompedString);
  hostUtf8.push_back(std::byte{0});

  m_hostPath.set(reinterpret_cast<const char *>(hostUtf8.data()));
  m_isEmpty = false;
}

StringStorage ConnectionData::getCryptedPassword() const
{
  return m_defaultPassword;
}

void ConnectionData::setCryptedPassword(const StringStorage *hidePassword)
{
  m_hasPlainPassword = false;
  m_plainPassword.setString(_T(""));
  if (hidePassword == 0) {
    m_isSetPassword = false;
  } else {
    m_defaultPassword = *hidePassword;
    m_isSetPassword = true;
  }
}

StringStorage ConnectionData::getPlainPassword() const
{
  // Given in plain text, so exact, even if its first 8 bytes in UTF-8 end
  // in the middle of a character, which the decryption below cannot undo.
  if (m_hasPlainPassword) {
    return m_plainPassword;
  }

  // Transform password from hex-string to raw data. Characters outside
  // Latin-1 become '?', which is not a hex digit either. The loop below
  // reads two hex digits per byte, so a shorter string, as a .vnc file can
  // give, would be read past its end; the size is checked after the
  // conversion, which turns a surrogate pair into one '?'.
  std::vector<std::byte> hideChars = TextEncoding::toLatin1(m_defaultPassword);
  if (hideChars.size() != VncAuthentication::VNC_PASSWORD_SIZE * 2) {
    throw Exception(_T("The saved password is not valid"));
  }
  UINT8 encPassword[VncAuthentication::VNC_PASSWORD_SIZE];
  for (size_t i = 0; i < VncAuthentication::VNC_PASSWORD_SIZE; ++i) {
    std::stringstream passwordStream;
    passwordStream << static_cast<char>(hideChars[i * 2])
                   << static_cast<char>(hideChars[i * 2 + 1]);
    int ordOfSymbol = 0;
    passwordStream >> std::hex >> ordOfSymbol;
    encPassword[i] = static_cast<UINT8>(ordOfSymbol);
  }
  // Decrypt password: the first 8 bytes of the password in UTF-8, padded
  // with zeros, so without a terminator if it is 8 bytes or longer.
  UINT8 plainPassword[VncAuthentication::VNC_PASSWORD_SIZE];
  VncPassCrypt::getPlainPass(plainPassword, encPassword);

  return TextEncoding::fromUtf8(std::as_bytes(std::span(plainPassword)));
}

void ConnectionData::setPlainPassword(const StringStorage *password)
{
  // The same bytes as VncAuthentication uses for the key.
  std::vector<std::byte> passwordUtf8 = TextEncoding::toUtf8(*password);
  UINT8 plainPassword[VncAuthentication::VNC_PASSWORD_SIZE];
  UINT8 encryptedPassword[VncAuthentication::VNC_PASSWORD_SIZE];
  memset(plainPassword, 0, VncAuthentication::VNC_PASSWORD_SIZE);
  memcpy(plainPassword,
         passwordUtf8.data(),
         std::min<size_t>(VncAuthentication::VNC_PASSWORD_SIZE,
                          passwordUtf8.size()));
  VncPassCrypt::getEncryptedPass(encryptedPassword, plainPassword);
  UINT8 hidePasswordChars[VncAuthentication::VNC_PASSWORD_SIZE * 2];
  for (size_t i = 0; i < VncAuthentication::VNC_PASSWORD_SIZE; ++i) {
    std::stringstream passwordStream;
    int ordOfSymbol = encryptedPassword[i];
    passwordStream << std::hex << std::setw(2) << std::setfill('0') << ordOfSymbol;
    passwordStream >> hidePasswordChars[i * 2] >> hidePasswordChars[i * 2 + 1];
  }

  // save password
  m_defaultPassword = TextEncoding::fromLatin1(std::as_bytes(std::span(hidePasswordChars)));
  m_plainPassword = *password;
  m_hasPlainPassword = true;
  m_isSetPassword = true;
}

StringStorage ConnectionData::getHost() const
{
  return fromHostPath(m_hostPath.get());
}

void ConnectionData::getReducedHost(StringStorage *strHost) const
{
  *strHost = fromHostPath(m_hostPath.getVncHost());
}

int ConnectionData::getPort() const
{
  return m_hostPath.getVncPort();
}
