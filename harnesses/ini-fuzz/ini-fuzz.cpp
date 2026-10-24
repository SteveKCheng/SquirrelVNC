// Copyright (C) 2026 Steve Cheng.
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

//
// ini-fuzz: feeds fuzz inputs, as the contents of a .ini or .vnc file, to
// IniFileSettingsManager::loadData(), then reads and changes settings, as
// the viewer does with a .vnc file, ConnectionConfig included. A .vnc file
// can come from anywhere. See notes/testing.md.
//
// As rfb-fuzz: in the ReleaseFuzz configuration a libFuzzer program; in
// the others, a main() of its own runs each file named once.
//

#include "client-config-lib/ConnectionConfig.h"
#include "config-lib/IniFileSettingsManager.h"
#include "rfb/HostPath.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <span>
#include <vector>

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  IniFileSettingsManager ini;
  ini.loadData(std::as_bytes(std::span(data, size)));

  static const TCHAR *const SECTIONS[] = { _T("connection"), _T("options"), _T("") };
  static const TCHAR *const NAMES[] = { _T("host"), _T("port"), _T("password"),
                                        _T("quality"), _T("scale_den"), _T("") };
  for (const TCHAR *section : SECTIONS) {
    ini.setApplicationName(section);
    for (const TCHAR *name : NAMES) {
      StringStorage text;
      int number = 0;
      bool flag = false;
      ini.getString(name, &text);
      ini.getInt(name, &number);
      ini.getBoolean(name, &flag);
    }
  }

  // As ViewerCmdLine::parseOptionsFile() gives the host to ConnectionData,
  // whose HostPath parses it.
  ini.setApplicationName(_T("connection"));
  StringStorage host;
  if (ini.getString(_T("host"), &host)) {
    HostPath hostPath(host.getString());
  }

  // As ViewerCmdLine::parseOptionsFile() loads the options.
  ini.setApplicationName(_T("options"));
  ConnectionConfig config;
  config.loadFromStorage(&ini);
  int percent = config.getScaleNumerator() * 100 / config.getScaleDenominator();
  (void)percent;

  // And changes, as saving a session does.
  ini.setApplicationName(_T("connection"));
  ini.setString(_T("host"), _T("example.com"));
  ini.setUINT(_T("port"), 5901);
  ini.setApplicationName(_T("options"));
  config.saveToStorage(&ini);
  return 0;
}

#ifndef SQUIRREL_LIBFUZZER

//
// Run each file named on the command line once.
//
// Post-conditions: returns 0 if every file was read and run, 2 if a file
// could not be read or no file was named. A finding crashes the program,
// or in the ReleaseAsan build ends it with ASan's report.
//
int _tmain(int argc, TCHAR *argv[])
{
  if (argc < 2) {
    _ftprintf(stderr, _T("Usage: ini-fuzz FILE...\n")
                      _T("Runs each file once, as libFuzzer would.\n"));
    return 2;
  }
  for (int i = 1; i < argc; i++) {
    FILE *file = 0;
    if (_tfopen_s(&file, argv[i], _T("rb")) != 0) {
      _ftprintf(stderr, _T("Cannot open %s\n"), argv[i]);
      return 2;
    }
    std::vector<uint8_t> data;
    uint8_t buffer[65536];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
      data.insert(data.end(), buffer, buffer + count);
    }
    bool ok = ferror(file) == 0;
    fclose(file);
    if (!ok) {
      _ftprintf(stderr, _T("Cannot read %s\n"), argv[i]);
      return 2;
    }
    LLVMFuzzerTestOneInput(data.data(), data.size());
    _ftprintf(stderr, _T("ok %s\n"), argv[i]);
  }
  return 0;
}

#endif // SQUIRREL_LIBFUZZER
