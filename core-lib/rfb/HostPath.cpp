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

#include "HostPath.h"

#include <tchar.h>

const size_t HostPath::m_SSH_USER_MAX_CHARS = 32;
const size_t HostPath::m_SSH_HOST_MAX_CHARS = 255;
const size_t HostPath::m_SSH_PORT_MAX_CHARS = 5;
const size_t HostPath::m_VNC_HOST_MAX_CHARS = 255;
const size_t HostPath::m_VNC_PORT_MAX_CHARS = 5;
const int HostPath::m_MAX_PORT = 65535;
// Compute the maximal length of a valid path. Note that there can be no
// more than five delimiter characters: "user@sshhost:port/vnchost::port".
const size_t HostPath::m_MAX_PATH_LEN =
(HostPath::m_SSH_USER_MAX_CHARS + HostPath::m_SSH_HOST_MAX_CHARS + HostPath::m_SSH_PORT_MAX_CHARS +
  HostPath::m_VNC_HOST_MAX_CHARS + HostPath::m_VNC_PORT_MAX_CHARS + 5);

HostPath::HostPath()
  : m_sshPort(0),
    m_vncPort(0),
    m_defaultPort(5900)
{
}

HostPath::HostPath(const TCHAR *path, int defaultPort)
  : m_sshPort(0),
    m_vncPort(0),
    m_defaultPort(defaultPort)
{
  set(path);
}

HostPath::~HostPath()
{
}

bool
HostPath::set(const TCHAR *path)
{
  // Forget previous path if one was set earlier.
  clear();

  // Check the path length and save a copy for this object. One character
  // more than the limit is looked at, so that a longer path is rejected
  // rather than cut short.
  if (path == 0) {
    return false;
  }
  size_t pathLen = _tcsnlen(path, m_MAX_PATH_LEN + 1);
  if (pathLen < 1 || pathLen > m_MAX_PATH_LEN) {
    return false;
  }
  m_path.setString(path);

  // Perform initial parsing and checking of the path.
  size_t tokens[4];
  parsePath(tokens);
  if ( tokens[0] + tokens[2] + tokens[3] == 0 ||
       (tokens[1] != 0 && tokens[0] == 0) ) {
    clear();
    return false;
  }
  const TCHAR *tokenStart = m_path.getString();

  // Handle SSH host name.
  if (tokens[0] != 0) {
    size_t hostLen = tokens[0];
    if (hostLen > m_SSH_HOST_MAX_CHARS) {
      clear();
      return false;
    }
    m_sshHost = StringStorage(StringStorage::StdString(tokenStart, hostLen));
    m_sshPort = 22;
    tokenStart += tokens[0];

    // Handle SSH port number.
    if (tokens[1] != 0) {
      size_t portLen = tokens[1] - 1;
      if (portLen < 1 ||
          portLen > m_SSH_PORT_MAX_CHARS ||
          *tokenStart != _T(':') ||
          _tcsspn(tokenStart + 1, _T("0123456789")) != portLen) {
        clear();
        return false;
      }
      m_sshPort = _ttoi(tokenStart + 1);
      if (m_sshPort > m_MAX_PORT) {
        clear();
        return false;
      }
      tokenStart += tokens[1];
    }

    tokenStart++; // skip '/'
  }

  // Handle VNC host name.
  if (tokens[2] == 0) {
    m_vncHost.setString(_T("localhost"));
  } else {
    size_t hostLen = tokens[2];
    if (hostLen > m_VNC_HOST_MAX_CHARS) {
      clear();
      return false;
    }
    m_vncHost = StringStorage(StringStorage::StdString(tokenStart, hostLen));
  }
  tokenStart += tokens[2];

  // Handle VNC display or port number.
  if (tokens[3] == 0) {
    m_vncPort = m_defaultPort;
  } else {
    size_t portLen = tokens[3] - 1;
    if (portLen < 1 || *tokenStart != _T(':')) {
      clear();
      return false;
    }
    const TCHAR* portStart = tokenStart + 1;
    bool twoColons = (*portStart == _T(':'));
    if (twoColons) {
      portStart++;
      portLen--;
    }
    if (portLen < 1 ||
        portLen > m_VNC_PORT_MAX_CHARS ||
        _tcsspn(portStart, _T("0123456789")) != portLen) {
      clear();
      return false;
    }
    m_vncPort = _ttoi(portStart);
    if (!twoColons && m_vncPort >= 0 && m_vncPort <= 99) {
      m_vncPort += m_defaultPort;
    }
    if (m_vncPort > m_MAX_PORT) {
      clear();
      return false;
    }
  }

  // Perform strict validation of m_vncHost (and m_sshHost if present).
  if (!validateHostNames()) {
    clear();
    return false;
  }

  return true;
}

void
HostPath::clear()
{
  m_path.setString(_T(""));
  m_sshHost.setString(_T(""));
  m_vncHost.setString(_T(""));
  m_sshPort = 0;
  m_vncPort = 0;
}

void
HostPath::parsePath(size_t results[]) const
{
  memset(results, 0, 4 * sizeof(size_t));

  if (m_path.isEmpty())
    return;

  const TCHAR* path = m_path.getString();
  const TCHAR* vncHostStart = path;

  const TCHAR* slashPtr = _tcschr(path, _T('/'));
  if (slashPtr != 0) {
    results[0] = _tcscspn(path, _T(":/"));
    results[1] = (slashPtr - path) - results[0];
    vncHostStart = slashPtr + 1;
  }

  const TCHAR *colonPtr = _tcschr(vncHostStart, _T(':'));
  if (colonPtr != 0) {
    results[2] = colonPtr - vncHostStart;
    results[3] = _tcsnlen(colonPtr, m_MAX_PATH_LEN);
  } else {
    results[2] = _tcsnlen(vncHostStart, m_MAX_PATH_LEN);
  }
}

bool
HostPath::validateHostNames() const
{
  const TCHAR* acceptChars =
    _T(".-_ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz01234567890");

  if (!m_sshHost.isEmpty()) {
    const TCHAR* sshHostPtr = m_sshHost.getString();
    size_t sshHostLen = m_sshHost.getLength();

    const TCHAR *sobakaPtr = _tcschr(sshHostPtr, _T('@'));
    if (sobakaPtr != 0) {
      const TCHAR* sshUserPtr = sshHostPtr;
      size_t sshUserLen = sobakaPtr - sshHostPtr;
      sshHostPtr = sobakaPtr + 1;
      sshHostLen = sshHostLen - sshUserLen - 1;

      if (sshUserLen < 1 || sshUserLen > m_SSH_USER_MAX_CHARS ||
          _tcsspn(sshUserPtr, acceptChars) != sshUserLen) {
        return false;
      }
    }

    if (sshHostLen < 1 || sshHostLen > m_SSH_HOST_MAX_CHARS ||
        _tcsspn(sshHostPtr, acceptChars) != sshHostLen) {
      return false;
    }
  }

  if (m_vncHost.isEmpty())
    return false;

  size_t vncHostLen = m_vncHost.getLength();
  if (vncHostLen < 1 || vncHostLen > m_VNC_HOST_MAX_CHARS ||
      _tcsspn(m_vncHost.getString(), acceptChars) != vncHostLen) {
    return false;
  }

  return true;
}
