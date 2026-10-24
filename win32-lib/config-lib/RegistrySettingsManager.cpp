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

#include "RegistrySettingsManager.h"

RegistrySettingsManager::RegistrySettingsManager()
{
}

RegistrySettingsManager::~RegistrySettingsManager()
{
}

void RegistrySettingsManager::setRegistryKey(HKEY rootKey, const TCHAR *entry)
{
  m_key = std::make_unique<RegistryKey>(rootKey, entry, true);
}

bool RegistrySettingsManager::getString(const TCHAR *name, StringStorage *value)
{
  return m_key && m_key->getValueAsString(name, value);
}

bool RegistrySettingsManager::setString(const TCHAR *name, const TCHAR *value)
{
  return m_key && m_key->setValueAsString(name, value);
}

bool RegistrySettingsManager::getBoolean(const TCHAR *name, bool *value)
{
  int intVal = 0;
  if (!getInt(name, &intVal)) {
    return false;
  }
  *value = intVal == 1;
  return true;
}

bool RegistrySettingsManager::setBoolean(const TCHAR *name, bool value)
{
  return setInt(name, value ? 1 : 0);
}

bool RegistrySettingsManager::setUINT(const TCHAR *name, UINT value)
{
  return setInt(name, (int)value);
}

bool RegistrySettingsManager::getInt(const TCHAR *name, int *value)
{
  return m_key && m_key->getValueAsInt32(name, value);
}

bool RegistrySettingsManager::setInt(const TCHAR *name, int value)
{
  return m_key && m_key->setValueAsInt32(name, value);
}

bool RegistrySettingsManager::getByte(const TCHAR *name, char *value)
{
  int intVal = 0;
  if (!getInt(name, &intVal)) {
    return false;
  }
  *value = (char)intVal;
  return true;
}

bool RegistrySettingsManager::setByte(const TCHAR *name, char value)
{
  return setInt(name, (int)value);
}
