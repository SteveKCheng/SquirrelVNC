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

#include "FolderListener.h"
#include <vector>

FolderListener::FolderListener(const TCHAR *folderPath)
{
  m_folderPath.setString(folderPath);
}

FolderListener::~FolderListener()
{
}

const FileInfo *FolderListener::getFilesInfo() const
{
  return m_filesInfo.data();
}

UINT32 FolderListener::getFilesCount() const
{
  // File::list() cannot return more than a UINT32 of names.
  return static_cast<UINT32>(m_filesInfo.size());
}

bool FolderListener::list()
{
  std::vector<StringStorage> fileNameList;
  m_filesInfo.clear();

  if (!m_folderPath.isEmpty()) {
    File folder(m_folderPath.getString());
    if (!folder.list(&fileNameList)) {
      return false;
    }
  } else {
    // Without roots, the list is empty.
    File::listRoots(&fileNameList);
  }

  m_filesInfo.resize(fileNameList.size());

  for (size_t i = 0; i < m_filesInfo.size(); i++) {

    const TCHAR *fileName = fileNameList[i].getString();
    const TCHAR *folderName = m_folderPath.getString();

    File file(folderName, fileName);
    FileInfo fileInfo(file);

    m_filesInfo[i] = fileInfo;

    //
    // All files in root folder is directories
    //

    if (m_folderPath.isEmpty()) {
      m_filesInfo[i].setFlags(FileInfo::DIRECTORY);
      m_filesInfo[i].setSize(0);
      m_filesInfo[i].setLastModified(0);
    }
  }

  return true;
}
