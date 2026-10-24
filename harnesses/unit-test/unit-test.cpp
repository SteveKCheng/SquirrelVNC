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
// unit-test: unit tests of classes that need no network or GUI. Each check
// prints PASS or FAIL; the exit code is 0 if all passed, 1 otherwise.
// Files go to a new directory under %TEMP%, removed at the end. See
// notes/testing.md.
//

#include "UnitTest.h"

#include "util/Exception.h"

#include <stdio.h>

static int g_failures = 0;
static int g_checks = 0;

void check(bool ok, const char *what)
{
  g_checks++;
  if (!ok) {
    g_failures++;
  }
  printf("%s %s\n", ok ? "PASS" : "FAIL", what);
}

int wmain()
{
  wchar_t temp[MAX_PATH];
  if (GetTempPathW(MAX_PATH, temp) == 0) {
    printf("Cannot find the temporary directory\n");
    return 2;
  }
  std::wstring dir = std::wstring(temp) + L"unit-test-" + std::to_wstring(GetCurrentProcessId());
  if (!CreateDirectoryW(dir.c_str(), nullptr)) {
    printf("Cannot create %ls\n", dir.c_str());
    return 2;
  }

  try {
    runIniTests(dir);
    runHostPathTests();
    runDecoderStoreTests();
    runFileTests(dir);
    runFileInfoListTests();
    runScopeExitTests();
    runExceptionTests();
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
