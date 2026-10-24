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
#include <stdlib.h>

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

  // A junction, which mklink makes without administrator rights (a
  // symbolic link would need a privilege or developer mode).
  std::wstring target = dir + L"\\target";
  std::wstring targetFile = target + L"\\kept.txt";
  std::wstring link = dir + L"\\link";
  CreateDirectoryW(target.c_str(), 0);
  HANDLE kept = CreateFileW(targetFile.c_str(), GENERIC_WRITE, 0, 0, CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL, 0);
  if (kept != INVALID_HANDLE_VALUE) {
    CloseHandle(kept);
  }
  std::wstring mklink = L"mklink /J \"" + link + L"\" \"" + target + L"\" >nul";
  bool made = _wsystem(mklink.c_str()) == 0;
  check(made && File(link.c_str()).isLink() && File(link.c_str()).isDirectory(),
        "File::isLink is true for a junction to a directory");
  check(!File(target.c_str()).isLink() && !File(targetFile.c_str()).isLink(),
        "File::isLink is false for a directory and a file");
  check(!File((dir + L"\\missing").c_str()).isLink(),
        "File::isLink is false for a missing file");
  check(File(link.c_str()).remove() && !File(link.c_str()).exists() &&
        File(targetFile.c_str()).exists(),
        "File::remove removes a junction, not what it points to");
  RemoveDirectoryW(link.c_str());
  DeleteFileW(targetFile.c_str());
  RemoveDirectoryW(target.c_str());

  std::vector<StringStorage> roots;
  bool rootsOk = File::listRoots(&roots) && !roots.empty();
  for (const StringStorage &root : roots) {
    rootsOk = rootsOk && root.getLength() == 2 && root.getString()[1] == _T(':');
  }
  check(rootsOk, "File::listRoots gives drive names such as C:");
}
