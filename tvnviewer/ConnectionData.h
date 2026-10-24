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

#ifndef _CONNECTION_DATA_
#define _CONNECTION_DATA_

#include "util/StringStorage.h"
#include "rfb/HostPath.h"

class ConnectionData final
{
public:
  ConnectionData();
  ConnectionData(const ConnectionData &connectionData);

  //
  // This methods is setter and getter of hostname or pair of hostname and port.
  //
  void setHost(const StringStorage *host);
  StringStorage getHost() const;

  //
  // This method return hostname without port.
  //
  void getReducedHost(StringStorage *strHost) const;

  //
  // This method return port.
  //
  int getPort() const;

  //
  // This method return true, if hostname isn't set.
  //
  bool isEmpty() const;

  //
  // This methods is setter and getters of password.
  //

  // Pre: isSetPassword(); the saved password is from setPlainPassword() or
  // setCryptedPassword().
  // Post: returns the password given to setPlainPassword(), whole; or else
  // the one given to setCryptedPassword(), decrypted: its first
  // VNC_PASSWORD_SIZE bytes in UTF-8, up to the first zero byte, decoded
  // as UTF-8. If those bytes end in the middle of a character, it becomes
  // U+FFFD, and VNC authentication will not get the key the password had.
  // Throws Exception if the saved password is not VNC_PASSWORD_SIZE * 2
  // characters long. Non-hex characters are not detected; they decrypt to
  // some other password.
  StringStorage getPlainPassword() const;

  // Pre: isSetPassword().
  // Post: returns the encrypted password as hex digits, as saved in a .vnc
  // file: the first VNC_PASSWORD_SIZE bytes of the password in UTF-8.
  StringStorage getCryptedPassword() const;

  // Pre: password is not null.
  // Post: the password is kept, and encrypted for getCryptedPassword();
  // isSetPassword() is true.
  void setPlainPassword(const StringStorage *password);
  // Pre: password is null, or the encrypted password as hex digits, as
  // saved in a .vnc file. It comes from the file unchecked; it is checked
  // when getPlainPassword() reads it.
  // Post: if password is null, no password is set; otherwise it is saved
  // and isSetPassword() is true.
  void setCryptedPassword(const StringStorage *password);
  bool isSetPassword() const;
  void resetPassword();

  void setIncoming(bool isIncoming);
  bool isIncoming() const;

protected:
  HostPath m_hostPath;

  // This flag is true, if host isn't set.
  bool m_isEmpty;

  // This flag is true, if password is set.
  bool m_isSetPassword;

  // This flag is true, if connection is incoming (e.g. listening mode).
  bool m_isIncoming;

  // Saved password is crypted.
  StringStorage m_defaultPassword;

  // The password as given to setPlainPassword(), if m_hasPlainPassword.
  StringStorage m_plainPassword;
  bool m_hasPlainPassword;
};


#endif
