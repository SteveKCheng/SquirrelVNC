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
// ini-test: unit tests of IniFileSettingsManager, the .ini and .vnc file
// reader and writer. Each check prints PASS or FAIL; the exit code is 0
// if all passed, 1 otherwise. Files go to a new directory under %TEMP%,
// removed at the end. See notes/testing.md.
//

#include "config-lib/IniFileSettingsManager.h"
#include "util/Exception.h"

#include <stdio.h>
#include <stdlib.h>

#include <climits>
#include <cstddef>
#include <span>
#include <string>
#include <vector>

static int g_failures = 0;
static int g_checks = 0;

static void check(bool ok, const char *what)
{
  g_checks++;
  if (!ok) {
    g_failures++;
  }
  printf("%s %s\n", ok ? "PASS" : "FAIL", what);
}

static std::vector<std::byte> bytes(const std::string &text)
{
  const std::byte *data = reinterpret_cast<const std::byte *>(text.data());
  return std::vector<std::byte>(data, data + text.size());
}

static std::wstring getString(IniFileSettingsManager &ini, const wchar_t *section,
                              const wchar_t *name, bool *found = 0)
{
  ini.setApplicationName(section);
  StringStorage value(_T("(unchanged)"));
  bool ok = ini.getString(name, &value);
  if (found != 0) {
    *found = ok;
  }
  return ok ? value.str() : L"(missing)";
}

static bool writeBytes(const std::wstring &path, const std::string &data)
{
  FILE *file = 0;
  if (_wfopen_s(&file, path.c_str(), L"wb") != 0) {
    return false;
  }
  bool ok = fwrite(data.data(), 1, data.size(), file) == data.size();
  return fclose(file) == 0 && ok;
}

static std::string readBytes(const std::wstring &path)
{
  FILE *file = 0;
  std::string data;
  if (_wfopen_s(&file, path.c_str(), L"rb") != 0) {
    return "(cannot open)";
  }
  char buffer[4096];
  size_t count;
  while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
    data.append(buffer, count);
  }
  fclose(file);
  return data;
}

// Post-conditions: returns the names of the files in directory.
static std::vector<std::wstring> listFiles(const std::wstring &directory)
{
  std::vector<std::wstring> names;
  WIN32_FIND_DATAW found;
  HANDLE search = FindFirstFileW((directory + L"\\*").c_str(), &found);
  if (search == INVALID_HANDLE_VALUE) {
    return names;
  }
  do {
    if (!(found.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY)) {
      names.push_back(found.cFileName);
    }
  } while (FindNextFileW(search, &found));
  FindClose(search);
  return names;
}

static void testEncodings()
{
  IniFileSettingsManager ini;

  check(ini.loadData(bytes("[s]\nk=Gr\xC3\xBC\xC3\x9F" "e\n")) &&
        getString(ini, L"s", L"k") == L"Gr\u00fc\u00dfe", "UTF-8 without byte-order mark");
  check(ini.loadData(bytes("\xEF\xBB\xBF[s]\r\nk=\xE6\x97\xA5\xE6\x9C\xAC\r\n")) &&
        getString(ini, L"s", L"k") == L"\u65e5\u672c", "UTF-8 with byte-order mark");

  std::string utf16("\xFF\xFE", 2);
  for (wchar_t c : std::wstring(L"[s]\r\nk=Gr\u00fc\u00dfe \u65e5\u672c\r\n")) {
    utf16.push_back(static_cast<char>(c & 0xFF));
    utf16.push_back(static_cast<char>(c >> 8));
  }
  check(ini.loadData(bytes(utf16)) && getString(ini, L"s", L"k") == L"Gr\u00fc\u00dfe \u65e5\u672c",
        "UTF-16LE with byte-order mark");

  // Not valid UTF-8, so the ANSI code page, as the profile functions wrote
  // it; 0xE9 is e with an acute accent in Windows-1252.
  bool loaded = ini.loadData(bytes("[s]\nk=caf\xE9\n"));
  if (GetACP() == 1252) {
    check(loaded && getString(ini, L"s", L"k") == L"caf\u00e9", "ANSI code page (1252)");
  } else {
    check(loaded && getString(ini, L"s", L"k") != L"(missing)", "ANSI code page (other)");
  }

  check(ini.loadData(bytes(std::string("[s]\nk=before\0after\nj=1\n", 23))) &&
        getString(ini, L"s", L"k") == L"before" && getString(ini, L"s", L"j") == L"(missing)",
        "text ends at a null character");
}

static void testParsing()
{
  IniFileSettingsManager ini;
  check(ini.loadData(bytes(
          "; comment\n"
          "# comment too\n"
          "[Connection]\n"
          "Host = example.com \n"
          "quoted=\"  spaced  \"\n"
          "dup=first\n"
          "dup=second\n"
          "empty=\n"
          "[options]\n"
          "n1=12\n n2 = -3\nn3=+4\nn4=abc\nn5=12x\nn6=2147483647\nn7=2147483648\n"
          "n8=-2147483648\nn9=-2147483649\nn10=\nn11=0x10\nb1=1\nb0=0\nb2=2\n")),
        "parse a typical file");

  check(getString(ini, L"connection", L"host") == L"example.com",
        "section and key names ignore case; spaces around the value go");
  check(getString(ini, L"connection", L"quoted") == L"  spaced  ",
        "a pair of quotes around a value is removed");
  check(getString(ini, L"connection", L"dup") == L"second", "a later duplicate key wins");
  bool found = false;
  check(getString(ini, L"connection", L"empty", &found) == L"" && found, "an empty value exists");
  check(getString(ini, L"connection", L"absent") == L"(missing)", "a missing key");
  check(getString(ini, L"nosection", L"host") == L"(missing)", "a missing section");

  ini.setApplicationName(L"options");
  struct { const wchar_t *name; bool ok; int value; } numbers[] = {
    { L"n1", true, 12 }, { L"n2", true, -3 }, { L"n3", true, 4 }, { L"n4", false, 0 },
    { L"n5", false, 0 }, { L"n6", true, INT_MAX }, { L"n7", false, 0 },
    { L"n8", true, INT_MIN }, { L"n9", false, 0 }, { L"n10", false, 0 },
    { L"n11", false, 0 }, { L"missing", false, 0 },
  };
  for (const auto &n : numbers) {
    int value = 777;
    bool ok = ini.getInt(n.name, &value);
    char what[64];
    sprintf_s(what, "getInt %ls", n.name);
    check(ok == n.ok && value == (n.ok ? n.value : 777), what);
  }
  bool b = false;
  check(ini.getBoolean(L"b1", &b) && b, "getBoolean 1");
  check(ini.getBoolean(L"b0", &b) && !b, "getBoolean 0");
  check(ini.getBoolean(L"b2", &b) && !b, "getBoolean 2 is false, as before");
}

static void testSetters()
{
  IniFileSettingsManager ini;
  ini.setApplicationName(L"s");
  check(ini.setString(L"k", L"v") && ini.keyExist(L"k"), "setString and keyExist");
  check(!ini.setString(L"k", 0), "setString of NULL fails");
  check(!ini.setString(L"k", L"a\nb") && getString(ini, L"s", L"k") == L"v",
        "a value with a line break is refused");
  check(!ini.setString(L"x\r", L"v") && !ini.keyExist(L"x\r"),
        "a name with a line break is refused");
  ini.setApplicationName(L"bad\nsection");
  check(!ini.setString(L"k", L"v"), "a section name with a line break is refused");
  ini.setApplicationName(L"s");
  check(ini.deleteKey(L"k") && !ini.keyExist(L"k"), "deleteKey");
  check(ini.deleteKey(L"never"), "deleteKey of a missing key succeeds");
  check(ini.setInt(L"i", -5) && ini.setUINT(L"u", 4000000000u) && ini.setBoolean(L"b", true),
        "setInt, setUINT, setBoolean");
  int i = 0;
  UINT u = 0;
  check(ini.getInt(L"i", &i) && i == -5, "getInt after setInt");
  // As before, a UINT goes through int, so values above INT_MAX do not load.
  check(!ini.getUINT(L"u", &u), "getUINT above INT_MAX fails, as getInt does");
}

static void testFiles(const std::wstring &dir)
{
  std::wstring path = dir + L"\\test.vnc";

  {
    IniFileSettingsManager ini(path.c_str());
    check(!ini.load(), "load of a missing file fails");
  }

  {
    IniFileSettingsManager ini(path.c_str(), L"connection");
    ini.setString(L"host", L"example.com");
    ini.setUINT(L"port", 5901);
    ini.setString(L"name", L"Gr\u00fc\u00dfe \u65e5\u672c");
    ini.setString(L"padded", L" x ");
    ini.setApplicationName(L"options");
    ini.setInt(L"quality", 6);
    bool threw = false;
    try {
      ini.save();
    } catch (const Exception &) {
      threw = true;
    }
    check(!threw, "save a new file");
  }
  std::string written = readBytes(path);
  check(written.compare(0, 3, "\xEF\xBB\xBF") != 0, "saved without a byte-order mark");
  check(written.find("host=example.com\r\n") != std::string::npos, "saved as key=value with CRLF");
  check(written.find("Gr\xC3\xBC\xC3\x9F" "e \xE6\x97\xA5\xE6\x9C\xAC") != std::string::npos,
        "saved in UTF-8");
  check(written.find("padded=\" x \"") != std::string::npos,
        "a value with spaces at its ends is saved quoted");

  {
    IniFileSettingsManager ini(path.c_str());
    check(ini.load(), "load the saved file");
    check(getString(ini, L"connection", L"name") == L"Gr\u00fc\u00dfe \u65e5\u672c" &&
          getString(ini, L"connection", L"padded") == L" x ",
          "the saved values load back");
    int quality = 0;
    ini.setApplicationName(L"options");
    check(ini.getInt(L"quality", &quality) && quality == 6, "the saved number loads back");
  }

  {
    // Replacing an existing file, from a document that was not loaded.
    IniFileSettingsManager ini(path.c_str(), L"connection");
    ini.setString(L"host", L"other.example");
    ini.save();
    IniFileSettingsManager again(path.c_str());
    check(again.load() && getString(again, L"connection", L"host") == L"other.example" &&
          getString(again, L"connection", L"port") == L"(missing)",
          "save replaces the whole file");
  }
  std::vector<std::wstring> files = listFiles(dir);
  check(files.size() == 1 && files[0] == L"test.vnc", "no temporary file is left");

  {
    std::wstring missingDir = dir + L"\\no-such-directory\\x.vnc";
    IniFileSettingsManager ini(missingDir.c_str(), L"s");
    ini.setString(L"k", L"v");
    bool threw = false;
    try {
      ini.save();
    } catch (const Exception &) {
      threw = true;
    }
    check(threw, "save to a missing directory throws");
  }

  {
    // A read-only file cannot be replaced; it is left as it was.
    std::wstring readOnly = dir + L"\\read-only.vnc";
    writeBytes(readOnly, "[s]\nk=old\n");
    SetFileAttributesW(readOnly.c_str(), FILE_ATTRIBUTE_READONLY);
    IniFileSettingsManager ini(readOnly.c_str(), L"s");
    ini.setString(L"k", L"new");
    bool threw = false;
    try {
      ini.save();
    } catch (const Exception &) {
      threw = true;
    }
    check(threw && readBytes(readOnly) == "[s]\nk=old\n", "a read-only file is left unchanged");
    SetFileAttributesW(readOnly.c_str(), FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(readOnly.c_str());
    check(listFiles(dir).size() == 1, "no temporary file is left after a failure");
  }

  {
    std::wstring big = dir + L"\\big.vnc";
    writeBytes(big, "[s]\nk=v\n" + std::string(IniFileSettingsManager::MAX_FILE_SIZE, ';'));
    IniFileSettingsManager ini(big.c_str());
    check(!ini.load() && getString(ini, L"s", L"k") == L"(missing)",
          "a file over MAX_FILE_SIZE is not loaded");
    DeleteFileW(big.c_str());

    std::wstring empty = dir + L"\\empty.vnc";
    writeBytes(empty, "");
    IniFileSettingsManager emptyIni(empty.c_str());
    check(emptyIni.load() && getString(emptyIni, L"s", L"k") == L"(missing)",
          "an empty file loads, with no settings");
    DeleteFileW(empty.c_str());
  }

  DeleteFileW(path.c_str());
}

int wmain()
{
  wchar_t temp[MAX_PATH];
  if (GetTempPathW(MAX_PATH, temp) == 0) {
    printf("Cannot find the temporary directory\n");
    return 2;
  }
  std::wstring dir = std::wstring(temp) + L"ini-test-" + std::to_wstring(GetCurrentProcessId());
  if (!CreateDirectoryW(dir.c_str(), 0)) {
    printf("Cannot create %ls\n", dir.c_str());
    return 2;
  }

  try {
    testEncodings();
    testParsing();
    testSetters();
    testFiles(dir);
  } catch (const Exception &e) {
    printf("FAIL unexpected exception: %ls\n", e.getMessage());
    g_failures++;
  }

  for (const std::wstring &name : listFiles(dir)) {
    std::wstring path = dir + L"\\" + name;
    SetFileAttributesW(path.c_str(), FILE_ATTRIBUTE_NORMAL);
    DeleteFileW(path.c_str());
  }
  RemoveDirectoryW(dir.c_str());

  printf("\n%d of %d checks passed\n", g_checks - g_failures, g_checks);
  return g_failures == 0 ? 0 : 1;
}
