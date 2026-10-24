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

#include "RegistryKey.h"
#include <vector>

RegistryKey::RegistryKey(HKEY rootKey, const TCHAR *entry,
                         bool createIfNotExists)
: m_key(nullptr),
  m_rootKey(nullptr)
{
  initialize(rootKey, entry, createIfNotExists);
}

RegistryKey::RegistryKey()
: m_key(nullptr),
  m_rootKey(nullptr)
{
}

RegistryKey::~RegistryKey()
{
  close();
}

void RegistryKey::open(HKEY rootKey,
                       const TCHAR *entry,
                       bool createIfNotExists)
{
  initialize(rootKey, entry, createIfNotExists);
}

bool RegistryKey::deleteSubKey(const TCHAR *subkey)
{
  if (!isOpened()) {
    return false;
  }

  return RegDeleteKey(m_key, subkey) == ERROR_SUCCESS;
}

bool RegistryKey::deleteValue(const TCHAR *name)
{
  if (!isOpened()) {
    return false;
  }

  return RegDeleteValue(m_key, name) == ERROR_SUCCESS;
}

bool RegistryKey::setValueAsInt32(const TCHAR *name, int value)
{
  if (!isOpened()) {
    return false;
  }

  return RegSetValueEx(m_key, name, 0, REG_DWORD, (BYTE *)&value, sizeof(value)) == ERROR_SUCCESS;
}

bool RegistryKey::setValueAsString(const TCHAR *name, const TCHAR *value)
{
  if (!isOpened()) {
    return false;
  }

  size_t origSize = (_tcslen(value) + 1) * sizeof(TCHAR);
  DWORD size = (DWORD)origSize;
  _ASSERT(size == origSize);
  return RegSetValueEx(m_key, name, 0, REG_SZ, (BYTE *)value, size) == ERROR_SUCCESS;
}

bool RegistryKey::getValueAsInt32(const TCHAR *name, int *out)
{
  if (!isOpened()) {
    return false;
  }

  // The type is checked, or a string of up to four bytes would be read as
  // a number.
  DWORD type = 0;
  DWORD value = 0;
  DWORD size = sizeof(value);
  if (RegQueryValueEx(m_key, name, nullptr, &type, (BYTE *)&value, &size) != ERROR_SUCCESS ||
      type != REG_DWORD || size != sizeof(value)) {
    return false;
  }
  *out = (int)value;
  return true;
}

bool RegistryKey::getValueAsString(const TCHAR *name, StringStorage *out)
{
  if (!isOpened()) {
    return false;
  }

  DWORD type = 0;
  DWORD size;

  if (RegQueryValueEx(m_key, name, nullptr, &type, nullptr, &size) != ERROR_SUCCESS ||
      (type != REG_SZ && type != REG_EXPAND_SZ)) {
    return false;
  }

  std::vector<TCHAR> buffer(size + 1);

  // The value may have changed since the size was read.
  if (RegQueryValueEx(m_key, name, nullptr, &type, (BYTE *)&buffer[0], &size) != ERROR_SUCCESS ||
      (type != REG_SZ && type != REG_EXPAND_SZ)) {
    return false;
  }

  if (buffer[size] != _T('\0')) {
    buffer[size] = _T('\0');
  }

  out->setString(&buffer[0]);

  return true;
}

bool RegistryKey::isOpened()
{
  return m_key != nullptr;
}

void RegistryKey::close()
{
  if (isOpened()) {
    if (m_key != m_rootKey) {
      RegCloseKey(m_key);
    }
    m_key = nullptr;
  }
}

void RegistryKey::initialize(HKEY rootKey, const TCHAR *entry, bool createIfNotExists)
{
  // open() may be called on an open key; its handle would leak.
  close();
  m_rootKey = rootKey;
  m_key = nullptr;

  m_entry.setString(entry);

  if (!m_entry.isEmpty() && !m_entry.endsWith(_T('\\'))) {
    m_entry.appendString(_T("\\"));
  }

  tryOpenSubKey(m_rootKey, m_entry.getString(), &m_key, createIfNotExists);
}

bool RegistryKey::tryOpenSubKey(HKEY key, const TCHAR *subkey, HKEY *openedKey, bool createIfNotExists)
{
  if (RegOpenKey(key, subkey, openedKey) != ERROR_SUCCESS) {
    if (createIfNotExists) {
      DWORD ret = RegCreateKey(key, subkey, openedKey);
      if (ret != ERROR_SUCCESS) {
        return false;
      } // if cannot create key.
    } else {
      return false;
    }
  } // if cannot open key

  return true;
}
