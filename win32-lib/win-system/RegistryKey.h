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

#ifndef _REGISTRY_KEY_H_
#define _REGISTRY_KEY_H_

#include "util/CommonHeader.h"

class RegistryKey
{
public:
  RegistryKey(HKEY rootKey, const TCHAR *entry, bool createIfNotExists = true);
  // Default contructor for a defer initialization.
  RegistryKey();

public:

  virtual ~RegistryKey();


  // Defer initialization. Can be used only when it has been
  // created by the default constructor.
  void open(HKEY rootKey, const TCHAR *entry,
            bool createIfNotExists = true);

  // Deletes subkey, which must have no subkeys, from this registry entry.
  bool deleteSubKey(const TCHAR *subkey);
  // Deletes value from this registry entry.
  bool deleteValue(const TCHAR *name);

  //
  // Set value methods
  //

  bool setValueAsInt32(const TCHAR *name, int value);
  bool setValueAsString(const TCHAR *name, const TCHAR *value);

  //
  // Get value methods
  //

  bool getValueAsInt32(const TCHAR *name, int *out);
  bool getValueAsString(const TCHAR *name, StringStorage *out);

  // Returns true if key is valid and opened.
  bool isOpened();
  // Closes registry entry if it's opened.
  void close();

private:

  // Helper method to avoid code duplicate in class constructor.
  void initialize(HKEY rootKey, const TCHAR *entry, bool createIfNotExists);

  /**
   * Opens subkey or creates it if it's does not exists.
   * @param [in] key root key.
   * @param [in] subkey subkey name (can include more than one subkey).
   * @param [out] openedKey created or opened key.
   * @param [in] createIfNotExists flag determinates to create key if it does not exists.
   * @return true if operation successfull executed, false otherwise.
   */
  static bool tryOpenSubKey(HKEY key, const TCHAR *subkey,
                            HKEY *openedKey, bool createIfNotExists);

protected:
  // Registry entry associated with this class instance.
  HKEY m_key;
  // Root registry key (local machine, current user, etc).
  HKEY m_rootKey;
  // Registry entry name.
  StringStorage m_entry;
};

#endif
