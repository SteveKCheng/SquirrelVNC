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

#ifndef _REGISTRY_SETTINGS_MANAGER_
#define _REGISTRY_SETTINGS_MANAGER_

#include "util/StringStorage.h"
#include "win-system/RegistryKey.h"

#include "SettingsManager.h"

#include <memory>

//
// Settings as the values of one registry key.
//
// Each name is a value name directly in the key (a backslash in it is
// part of the name, not a path to a subkey). Integers, and so booleans,
// UINTs and bytes, are REG_DWORD values; strings are REG_SZ.
//
class RegistrySettingsManager : public SettingsManager
{
public:
  // Post-conditions: no key is set; every get and set fails until
  // setRegistryKey() is called.
  RegistrySettingsManager();
  virtual ~RegistrySettingsManager();

  RegistrySettingsManager(const RegistrySettingsManager &) = delete;
  RegistrySettingsManager &operator=(const RegistrySettingsManager &) = delete;
  RegistrySettingsManager(RegistrySettingsManager &&) = delete;
  RegistrySettingsManager &operator=(RegistrySettingsManager &&) = delete;

  //
  // Post-conditions: the settings are the values of rootKey\entry, which
  // is created if it does not exist. If it cannot be opened or created,
  // every get and set fails.
  //
  void setRegistryKey(HKEY rootKey, const TCHAR *entry);

  virtual bool getString(const TCHAR *name, StringStorage *value);
  virtual bool setString(const TCHAR *name, const TCHAR *value);

  virtual bool getBoolean(const TCHAR *name, bool *value);
  virtual bool setBoolean(const TCHAR *name, bool value);

  virtual bool setUINT(const TCHAR *name, UINT value);

  virtual bool getInt(const TCHAR *name, int *value);
  virtual bool setInt(const TCHAR *name, int value);

  virtual bool getByte(const TCHAR *name, char *value);
  virtual bool setByte(const TCHAR *name, char value);

private:
  std::unique_ptr<RegistryKey> m_key;
};

#endif
