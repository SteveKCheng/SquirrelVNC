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

#include "IniFileSettingsManager.h"

#include "util/Exception.h"
#include "util/ScopeExit.h"
#include "util/TextEncoding.h"
#include "win-system/SystemException.h"

#include <algorithm>
#include <climits>
#include <crtdbg.h>
#include <span>
#include <string>
#include <vector>

namespace
{
  //
  // Pre-conditions: path is not empty.
  // Post-conditions: returns true and the file's contents in data if it
  // exists, could be read, and is at most maxSize bytes; otherwise false.
  //
  bool readFile(const StringStorage &path, size_t maxSize, std::vector<std::byte> *data)
  {
    HANDLE file = CreateFile(path.getString(), GENERIC_READ, FILE_SHARE_READ, 0,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
      return false;
    }
    ScopeExit closeFile([&] { CloseHandle(file); });
    bool ok = false;
    LARGE_INTEGER size;
    if (GetFileSizeEx(file, &size) && size.QuadPart >= 0 &&
        static_cast<unsigned long long>(size.QuadPart) <= maxSize) {
      data->resize(static_cast<size_t>(size.QuadPart));
      size_t done = 0;
      ok = true;
      while (ok && done < data->size()) {
        DWORD count = 0;
        // maxSize is far below 4 GiB, so the size fits a DWORD.
        ok = ReadFile(file, data->data() + done,
                      static_cast<DWORD>(data->size() - done), &count, 0) != 0 &&
             count > 0;
        done += count;
      }
    }
    return ok;
  }

  //
  // Post-conditions: returns text in the ANSI code page converted to UTF-8,
  // up to its first null character. This is the one place left for the
  // ANSI code page: the Win32 profile functions wrote .ini files in it.
  //
  std::vector<std::byte> ansiToUtf8(std::span<const std::byte> bytes)
  {
    if (bytes.empty()) {
      return std::vector<std::byte>();
    }
    // MAX_FILE_SIZE keeps the size far below INT_MAX.
    const char *text = reinterpret_cast<const char *>(bytes.data());
    int size = static_cast<int>(bytes.size());
    int length = MultiByteToWideChar(CP_ACP, 0, text, size, 0, 0);
    if (length <= 0) {
      return std::vector<std::byte>();
    }
    StringStorage::StdString wide(static_cast<size_t>(length), L'\0');
    MultiByteToWideChar(CP_ACP, 0, text, size, wide.data(), length);
    // The StdString constructor ends the text at a null character.
    return TextEncoding::toUtf8(StringStorage(std::move(wide)));
  }

  //
  // Post-conditions: returns the file's text in UTF-8, without a byte-order
  // mark, as described for IniFileSettingsManager::load().
  //
  std::vector<std::byte> toUtf8Text(std::span<const std::byte> data)
  {
    std::span<const std::byte> bytes(data);
    static const std::byte UTF8_BOM[] = { std::byte{0xEF}, std::byte{0xBB}, std::byte{0xBF} };
    static const std::byte UTF16LE_BOM[] = { std::byte{0xFF}, std::byte{0xFE} };

    if (bytes.size() >= 3 && std::equal(UTF8_BOM, UTF8_BOM + 3, bytes.begin())) {
      // Invalid sequences become U+FFFD when SimpleIni converts the text.
      bytes = bytes.subspan(3);
      return std::vector<std::byte>(bytes.begin(), bytes.end());
    }
    if (bytes.size() >= 2 && std::equal(UTF16LE_BOM, UTF16LE_BOM + 2, bytes.begin())) {
      bytes = bytes.subspan(2);
      // An odd last byte is dropped.
      StringStorage::StdString wide(bytes.size() / 2, L'\0');
      for (size_t i = 0; i < wide.size(); i++) {
        wide[i] = static_cast<wchar_t>(std::to_integer<unsigned>(bytes[2 * i]) |
                                       std::to_integer<unsigned>(bytes[2 * i + 1]) << 8);
      }
      return TextEncoding::toUtf8(StringStorage(std::move(wide)));
    }
    if (TextEncoding::isValidUtf8(bytes)) {
      return std::vector<std::byte>(bytes.begin(), bytes.end());
    }
    return ansiToUtf8(bytes);
  }

  //
  // Post-conditions: returns true if text has a line break.
  //
  bool hasLineBreak(const TCHAR *text)
  {
    return _tcspbrk(text, _T("\r\n")) != 0;
  }

  //
  // Post-conditions: returns true and the number in value if text is a
  // decimal number, with an optional sign, within int's range; otherwise
  // returns false and leaves value unchanged.
  //
  bool parseInt(const TCHAR *text, int *value)
  {
    bool negative = false;
    if (*text == _T('-') || *text == _T('+')) {
      negative = *text == _T('-');
      text++;
    }
    if (*text == _T('\0')) {
      return false;
    }
    long long result = 0;
    for (; *text != _T('\0'); text++) {
      if (*text < _T('0') || *text > _T('9')) {
        return false;
      }
      result = result * 10 + (*text - _T('0'));
      // INT_MAX + 1 is allowed until the sign is applied, for INT_MIN.
      if (result > static_cast<long long>(INT_MAX) + 1) {
        return false;
      }
    }
    if (negative) {
      result = -result;
    }
    if (result > INT_MAX) {
      return false;
    }
    *value = static_cast<int>(result);
    return true;
  }

  //
  // Post-conditions: returns the directory part of path, with its last
  // separator ("C:\" rather than "C:", which names the current directory
  // of drive C), or "." if it has none.
  //
  StringStorage directoryOf(const StringStorage &path)
  {
    size_t last = path.str().find_last_of(_T("\\/"));
    if (last == StringStorage::StdString::npos) {
      return StringStorage(_T("."));
    }
    StringStorage directory;
    path.getSubstring(&directory, 0, last);
    return directory;
  }
}

IniFileSettingsManager::IniFileSettingsManager(const TCHAR *pathToFile, const TCHAR *appName)
{
  m_pathToFile.setString(pathToFile);
  m_appName.setString(appName);
  configure();
}

IniFileSettingsManager::IniFileSettingsManager(const TCHAR *pathToFile)
{
  m_pathToFile.setString(pathToFile);
  configure();
}

IniFileSettingsManager::IniFileSettingsManager()
{
  configure();
}

IniFileSettingsManager::~IniFileSettingsManager()
{
}

void IniFileSettingsManager::configure()
{
  // Converted from and to UTF-8 with CP_UTF8 (SimpleIni's SI_CONVERT_WIN32).
  m_ini.SetUnicode(true);
  m_ini.SetMultiKey(false);
  // A value is one line, as for the profile functions.
  m_ini.SetMultiLine(false);
  // GetPrivateProfileString() removed a pair of quotes around a value.
  m_ini.SetQuotes(true);
  // key=value, as WritePrivateProfileString() wrote it.
  m_ini.SetSpaces(false);
}

void IniFileSettingsManager::setApplicationName(const TCHAR *appName)
{
  m_appName.setString(appName);
}

bool IniFileSettingsManager::load()
{
  std::vector<std::byte> data;
  if (!readFile(m_pathToFile, MAX_FILE_SIZE, &data)) {
    m_ini.Reset();
    configure();
    return false;
  }
  return loadData(data);
}

bool IniFileSettingsManager::loadData(std::span<const std::byte> data)
{
  m_ini.Reset();
  configure();

  if (data.size() > MAX_FILE_SIZE) {
    return false;
  }
  std::vector<std::byte> text = toUtf8Text(data);
  SI_Error result = m_ini.LoadData(reinterpret_cast<const char *>(text.data()), text.size());
  if (result < 0) {
    m_ini.Reset();
    configure();
    return false;
  }
  return true;
}

void IniFileSettingsManager::save()
{
  std::string text;
  if (m_ini.Save(text, false) < 0) {
    throw Exception(_T("Cannot convert the settings to text"));
  }

  StringStorage directory = directoryOf(m_pathToFile);
  TCHAR tempPath[MAX_PATH];
  // Creates the file, with a name of its own, in the target's directory, so
  // that it can replace the target without copying.
  if (GetTempFileName(directory.getString(), _T("svn"), 0, tempPath) == 0) {
    throw SystemException(_T("Cannot create a file to save the settings"));
  }

  // Each exception below is made, reading GetLastError(), before this
  // deletes the file; it is released once the file has taken its place.
  ScopeExit deleteTemp([&] { DeleteFile(tempPath); });

  {
    HANDLE file = CreateFile(tempPath, GENERIC_WRITE, 0, 0, CREATE_ALWAYS,
                             FILE_ATTRIBUTE_NORMAL, 0);
    if (file == INVALID_HANDLE_VALUE) {
      throw SystemException(_T("Cannot open a file to save the settings"));
    }
    // Closed at the end of this block, before the file is moved.
    ScopeExit closeFile([&] { CloseHandle(file); });
    size_t done = 0;
    bool ok = true;
    while (ok && done < text.size()) {
      DWORD count = 0;
      DWORD chunk = static_cast<DWORD>(std::min<size_t>(text.size() - done, 1 << 20));
      ok = WriteFile(file, text.data() + done, chunk, &count, 0) != 0;
      done += count;
    }
    // Written through before it replaces the old file.
    if (!ok || !FlushFileBuffers(file)) {
      throw SystemException(_T("Cannot write the settings"));
    }
  }

  const TCHAR *target = m_pathToFile.getString();
  if (GetFileAttributes(target) != INVALID_FILE_ATTRIBUTES) {
    // ReplaceFile() keeps the old file's attributes and access control list,
    // so a file the user made private stays private; without
    // REPLACEFILE_IGNORE_MERGE_ERRORS it fails rather than lose them.
    if (ReplaceFile(target, tempPath, 0, 0, 0, 0)) {
      deleteTemp.release();
      return;
    }
    DWORD error = GetLastError();
    if (error != ERROR_UNABLE_TO_MOVE_REPLACEMENT_2) {
      throw SystemException(_T("Cannot replace the settings file"), error);
    }
    // The old file was moved aside, under a name of the system's, but the
    // new one could not take its place: try once more to put it there.
  }
  if (!MoveFileEx(tempPath, target, MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH)) {
    throw SystemException(_T("Cannot save the settings file"));
  }
  deleteTemp.release();
}

bool IniFileSettingsManager::getString(const TCHAR *name, StringStorage *storage)
{
  const TCHAR *value = m_ini.GetValue(m_appName.getString(), name);
  if (value == 0) {
    return false;
  }
  storage->setString(value);
  return true;
}

bool IniFileSettingsManager::setString(const TCHAR *name, const TCHAR *value)
{
  if (value == NULL || name == NULL) {
    return false;
  }
  // With multi-line values off, SimpleIni writes a line break as it is,
  // which would start a new key or section in the file.
  if (hasLineBreak(name) || hasLineBreak(value) || hasLineBreak(m_appName.getString())) {
    return false;
  }
  return m_ini.SetValue(m_appName.getString(), name, value) >= 0;
}

bool IniFileSettingsManager::getBoolean(const TCHAR *name, bool *value)
{
  int intVal;
  if (!getInt(name, &intVal)) {
    return false;
  }
  *value = intVal == 1;
  return true;
}

bool IniFileSettingsManager::setBoolean(const TCHAR *name, bool value)
{
  return setInt(name, value ? 1 : 0);
}

bool IniFileSettingsManager::setUINT(const TCHAR *name, UINT value)
{
  StringStorage stringVal;
  stringVal.format(_T("%u"), value);

  return setString(name, stringVal.getString());
}

bool IniFileSettingsManager::getInt(const TCHAR *name, int *value)
{
  const TCHAR *text = m_ini.GetValue(m_appName.getString(), name);
  if (text == 0) {
    return false;
  }
  return parseInt(text, value);
}

bool IniFileSettingsManager::setInt(const TCHAR *name, int value)
{
  StringStorage stringVal;
  stringVal.format(_T("%d"), value);

  return setString(name, stringVal.getString());
}

bool IniFileSettingsManager::getByte(const TCHAR *name, char *value)
{
  return getIntAndCastTo<char>(name, value);
}

bool IniFileSettingsManager::setByte(const TCHAR *name, char value)
{
  return setInt(name, value);
}
