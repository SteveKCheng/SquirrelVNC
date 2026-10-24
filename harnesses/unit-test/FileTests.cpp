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
// Unit tests of File's directory listing.
//

#include "UnitTest.h"

#include "file-lib/File.h"

#include <algorithm>

void runFileTests(const std::wstring &dir)
{
  const wchar_t *names[] = { L"a.txt", L"b.txt", L"c.txt" };
  for (const wchar_t *name : names) {
    std::wstring path = dir + L"\\" + name;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, 0, CREATE_NEW,
                              FILE_ATTRIBUTE_NORMAL, 0);
    if (file != INVALID_HANDLE_VALUE) {
      CloseHandle(file);
    }
  }

  std::vector<StringStorage> list;
  check(File(dir.c_str()).list(&list), "File::list lists a directory");
  std::vector<std::wstring> found;
  for (const StringStorage &name : list) {
    found.push_back(name.getString());
  }
  // Other suites may have left files of their own in dir.
  std::sort(found.begin(), found.end());
  check(std::includes(found.begin(), found.end(), std::begin(names), std::end(names)) &&
        std::count(found.begin(), found.end(), L".") == 0 &&
        std::count(found.begin(), found.end(), L"..") == 0,
        "File::list gives the names of the files, without . and ..");

  std::vector<StringStorage> unchanged(1, StringStorage(_T("x")));
  check(!File((dir + L"\\missing").c_str()).list(&unchanged) &&
        unchanged.size() == 1 && unchanged[0].isEqualTo(_T("x")),
        "File::list fails on a missing directory, leaving the list");

  std::vector<StringStorage> roots;
  bool rootsOk = File::listRoots(&roots) && !roots.empty();
  for (const StringStorage &root : roots) {
    rootsOk = rootsOk && root.getLength() == 2 && root.getString()[1] == _T(':');
  }
  check(rootsOk, "File::listRoots gives drive names such as C:");
}
