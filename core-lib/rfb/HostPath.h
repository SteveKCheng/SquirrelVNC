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

#ifndef __RFB_HOST_PATH_H_INCLUDED__
#define __RFB_HOST_PATH_H_INCLUDED__

#include "util/StringStorage.h"

class HostPath {
public:
  HostPath();
  HostPath(const TCHAR *path, int defaultPort = 5900);
  virtual ~HostPath();

  //
  // Parses path, which names a VNC server and optionally an SSH host to
  // tunnel through, as "[[user@]sshhost[:sshport]/][vnchost][:display]"
  // or with "::port" in place of ":display", and keeps its parts.
  // vnchost defaults to "localhost", the SSH port to 22, and the VNC port
  // to the default port.  A display number from 0 to 99 is added to the
  // default port; a larger number after one colon, or any number after
  // two, is the port itself.
  //
  // Returns true on success.  Returns false, and leaves the object empty
  // (isValid() is false), if path is null, empty or longer than
  // m_MAX_PATH_LEN characters, is not of the form above, names an invalid
  // host, or gives a port above 65535.
  //
  bool set(const TCHAR *path);

  bool isValid() const               { return !m_path.isEmpty(); }
  bool isSshHostSpecified() const    { return !m_sshHost.isEmpty(); }

  // Each returns the empty string if the part is not set.
  const StringStorage &get() const        { return m_path; }
  const StringStorage &getSshHost() const { return m_sshHost; }
  const int getSshPort() const            { return m_sshPort; }
  const StringStorage &getVncHost() const { return m_vncHost; }
  const int getVncPort() const            { return m_vncPort; }

private:
  static const size_t m_SSH_USER_MAX_CHARS;
  static const size_t m_SSH_HOST_MAX_CHARS;
  static const size_t m_SSH_PORT_MAX_CHARS;
  static const size_t m_VNC_HOST_MAX_CHARS;
  static const size_t m_VNC_PORT_MAX_CHARS;
  static const int m_MAX_PORT;
  static const size_t m_MAX_PATH_LEN;

  StringStorage m_path;
  StringStorage m_sshHost;
  int m_sshPort;
  StringStorage m_vncHost;
  int m_vncPort;

  //
  // Reset the object to its initial state (no path set).
  //
  void clear();

  //
  // Parse m_path[] and store lengths if tokens in the specified array
  // of four size_t elements. Up to four tokens are detected: SSH host
  // name (optionally including a user name), SSH port number, VNC
  // host name, VNC port or display number. Port number tokens include
  // colons at the their beginning. Note that the '/' character
  // between SSH-related and VNC-related parts is not counted in any
  // token length.
  //
  // Examples: "user@server:22/vnc::443"  ->  11, 3, 3, 5
  //           "host/:1"                  ->   4, 0, 0, 2
  //           ":1"                       ->   0, 0, 0, 2
  //
  void parsePath(size_t results[]) const;

  //
  // Return true if m_sshHost and m_vncHost are valid, false otherwise. It
  // checks the lengths and character sets of the strings. m_sshHost may be
  // empty, that does not make the function return false. However,
  // m_vncHost is mandatory so the function will return false if it is
  // empty.
  //
  bool validateHostNames() const;

  int m_defaultPort;
};

#endif // __RFB_HOST_PATH_H_INCLUDED__
