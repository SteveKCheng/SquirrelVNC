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

#ifndef _INI_FILE_SETTINGS_MANAGER_H_
#define _INI_FILE_SETTINGS_MANAGER_H_

#include "SettingsManager.h"

#include "util/StringStorage.h"

#include <cstddef>
#include <span>

// SimpleIni is provided by vcpkg (see vcpkg.json).
#include <SimpleIni.h>

//
// Settings in an .ini file, such as a .vnc file, held in memory.
//
// load() reads the file and save() writes it; the get and set methods work
// only on the copy in memory. They work on the section named by
// setApplicationName() (the "application name" of the Win32 profile
// functions this class used before).
//
// Differences from those functions, none of which matter for .vnc files:
// a key given twice takes its last value, not its first; lines starting
// with '#' are comments, as well as those starting with ';'; and numbers
// must be whole decimal numbers, or they count as missing.
//
class IniFileSettingsManager final : public SettingsManager
{
public:
  IniFileSettingsManager(const TCHAR *pathToFile, const TCHAR *appName);
  IniFileSettingsManager(const TCHAR *pathToFile);
  IniFileSettingsManager();

  ~IniFileSettingsManager() override;

  IniFileSettingsManager(const IniFileSettingsManager &) = delete;
  IniFileSettingsManager &operator=(const IniFileSettingsManager &) = delete;
  IniFileSettingsManager(IniFileSettingsManager &&) = delete;
  IniFileSettingsManager &operator=(IniFileSettingsManager &&) = delete;

  // Sets application name (section name)
  void setApplicationName(const TCHAR *appName);

  // Largest file that load() reads. .vnc files are well under 1 KiB.
  static const size_t MAX_FILE_SIZE = 1024 * 1024;

  //
  // Reads the file.
  //
  // Post-conditions: returns true if the file was read: its settings
  // replace those held. Returns false, holding no settings, if it does not
  // exist, cannot be read, or is larger than MAX_FILE_SIZE. The file may
  // come from anywhere; its text is UTF-8 (with or without a byte-order
  // mark), UTF-16LE with a byte-order mark, or else, as the Win32 profile
  // functions wrote it, in the ANSI code page. Text ends at a null
  // character.
  //
  bool load();

  //
  // Reads settings from the contents of a file, as load() does after
  // reading it; for tests and fuzzing.
  //
  // Post-conditions: returns true if data, which may be anything, was
  // parsed: its settings replace those held. Returns false, holding no
  // settings, if it is larger than MAX_FILE_SIZE or SimpleIni rejects it.
  //
  bool loadData(std::span<const std::byte> data);

  //
  // Writes the settings held to the file, in UTF-8 without a byte-order
  // mark, replacing it whole: the text goes to a new file in the same
  // directory, which then replaces the old one, keeping its attributes and
  // access control list. A crash or failure leaves either the old file or
  // the new one, never a mix.
  //
  // Pre-conditions: the path to the file is not empty.
  // Throws Exception (SystemException for a Win32 error) if the file
  // cannot be written; the file is then unchanged.
  //
  void save();

  //
  // Inherited from SettingsManager class
  //

  bool getString(const TCHAR *name, StringStorage *storage) override;
  // Returns false, changing nothing, if value is NULL, or if name, value
  // or the section name has a line break, which would break the file.
  bool setString(const TCHAR *name, const TCHAR *value) override;

  // True only for the value 1, as before.
  bool getBoolean(const TCHAR *name, bool *value) override;
  bool setBoolean(const TCHAR *name, bool value) override;

  bool setUINT(const TCHAR *name, UINT value) override;

  // Returns false, leaving value unchanged, if the key is missing or its
  // value is not a decimal number (optionally signed) within int's range.
  bool getInt(const TCHAR *name, int *value) override;
  bool setInt(const TCHAR *name, int value) override;

  bool getByte(const TCHAR *name, char *value) override;
  bool setByte(const TCHAR *name, char value) override;

protected:
  StringStorage m_appName;
  StringStorage m_pathToFile;

private:
  // Sets the options load() and the setters rely on.
  void configure();

  CSimpleIniW m_ini;

  //
  // Helper template method to avoid code duplicate
  // in such methods as getByte.
  //

  template<typename T> bool getIntAndCastTo(const TCHAR *name, T *value) {
    int intValue = 0;
    if (!getInt(name, &intValue)) {
      return false;
    }
    *value = (T)intValue;
    return true;
  };

};

#endif
