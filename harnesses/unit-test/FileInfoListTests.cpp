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
// Unit tests of FileInfoList, the tree of files that file transfers
// walk. A server's list of a directory can be millions of entries long.
//

#include "UnitTest.h"

#include "ft-client-lib/FileInfoList.h"

#include <memory>
#include <vector>

void runFileInfoListTests()
{
  // Enough that walking or freeing the list recursively would overflow
  // the 1 MiB stack.
  const size_t COUNT = 200000;
  std::vector<FileInfo> files;
  files.reserve(COUNT);
  for (size_t i = 0; i < COUNT; i++) {
    files.push_back(FileInfo(0, 0, 0, _T("f")));
  }
  FileInfo folder(0, 0, FileInfo::DIRECTORY, _T("dir"));

  std::unique_ptr<FileInfoList> root = std::make_unique<FileInfoList>(folder);
  root->setChild(files.data(), files.size());

  FileInfoList *last = root->getChild();
  size_t length = 1;
  while (last->getNext() != nullptr) {
    last = last->getNext();
    length++;
  }
  check(length == COUNT, "FileInfoList holds every file of a long list");
  check(last->getFirst() == root->getChild(), "FileInfoList finds the first of a long list");
  check(last->getRoot() == root.get(), "FileInfoList finds the root from the end of a long list");

  StringStorage path;
  last->getAbsolutePath(&path, _T('/'));
  check(path.isEqualTo(_T("dir/f")), "FileInfoList gives the path of a file in a directory");

  root.reset();
  check(true, "FileInfoList frees a long list");
}
