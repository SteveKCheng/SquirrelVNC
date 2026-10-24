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

#include "File.h"

#include "util/DateTime.h"
#include "util/ScopeExit.h"
#include <crtdbg.h>

TCHAR File::s_separatorChar = _T('\\');

File::File(const TCHAR *pathname)
{
  m_pathName.setString(pathname);
}

File::File(const TCHAR *parent, const TCHAR *child)
{
  m_pathName.setString(parent);

  if (!m_pathName.endsWith(File::s_separatorChar)) {

    TCHAR separator[2];
    separator[0] = File::s_separatorChar;
    separator[1] = '\0';

    m_pathName.appendString(&separator[0]);
  }

  m_pathName.appendString(child);
}

File::~File()
{
}

bool File::createNewFile() const
{
  return tryCreateFile(GENERIC_READ | GENERIC_WRITE, CREATE_NEW);
}

bool File::remove() const
{
  if (!exists()) {
    return false;
  }
  if ((isDirectory()) && (RemoveDirectory(m_pathName.getString()) != 0)) {
    return true;
  }
  if (isFile() && DeleteFile(m_pathName.getString()) != 0) {
    return true;
  }
  return false;
}

bool File::exists() const
{
  WIN32_FIND_DATA fileInfo;
  return getFileInfo(&fileInfo);
}

void File::getName(StringStorage *name) const
{
  if (m_pathName.getLength() == 0) {
    name->setString(_T(""));
    return ;
  }

  const TCHAR *buffer = m_pathName.getString();
  size_t i = m_pathName.getLength();

  for (; i > 0; i--) {
    if (buffer[i - 1] == File::s_separatorChar) {
      break;
    } // if found separator
  } // for every char in string

  m_pathName.getSubstring(name, i, m_pathName.getLength() - 1);
}

bool File::isFile() const
{
  if (exists() && !isDirectory()) {
    return true;
  }
  return false;
}

bool File::isDirectory() const
{
  WIN32_FIND_DATA fileInfo;
  if (getFileInfo(&fileInfo)) {
    if (fileInfo.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) {
      return true;
    } // if normal file
  } // if can get file info
  return false;
}

void File::getPath(StringStorage *pathname) const
{
  *pathname = m_pathName;
}

UINT64 File::lastModified() const
{
  WIN32_FIND_DATA fileInfo;

  if (!getFileInfo(&fileInfo)) {
    return 0;
  }

  DateTime dt(fileInfo.ftLastWriteTime);

  return dt.getTime();
}

UINT64 File::length() const
{
  WIN32_FIND_DATA fileInfo;

  if (!getFileInfo(&fileInfo)) {
    return 0;
  }

  INT64 maxDWORDPlusOne = 1 + (INT64)MAXDWORD;

  return fileInfo.nFileSizeHigh * maxDWORDPlusOne + fileInfo.nFileSizeLow;
}

bool File::list(std::vector<StringStorage> *fileList) const
{
  StringStorage folderPath(m_pathName.getString());
  folderPath.appendString(_T("\\*"));

  std::vector<StringStorage> names;

  HANDLE hfile;
  WIN32_FIND_DATA findFileData;

  //
  // Change error mode to avoid windows error message in message box
  // when we attemt to find first file on unmounted device
  //

  UINT savedErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS);

  hfile = FindFirstFile(folderPath.getString(), &findFileData);

  // Restore error mode
  SetErrorMode(savedErrorMode);

  if (hfile == INVALID_HANDLE_VALUE) {
    return false;
  }
  ScopeExit closeFind([&] { FindClose(hfile); });

  do {

    //
    // Skip "fake" file names
    //

    if (_tcscmp(findFileData.cFileName, _T(".")) == 0 ||
        _tcscmp(findFileData.cFileName, _T("..")) == 0) {
      continue;
    }

    names.push_back(StringStorage(findFileData.cFileName));

  } while (FindNextFile(hfile, &findFileData));

  *fileList = std::move(names);
  return true;
}

bool File::listRoots(std::vector<StringStorage> *rootList)
{
  TCHAR drivesList[256];

  // The size excludes the final null character. A larger result is the
  // size needed, and nothing is written.
  DWORD length = GetLogicalDriveStrings(255, drivesList);
  if (length == 0 || length > 255) {
    return false;
  }

  std::vector<StringStorage> roots;
  // Each name ends with a null character, and the list with one more.
  for (const TCHAR *drive = drivesList; *drive != _T('\0'); drive += _tcslen(drive) + 1) {
    StringStorage root(drive);
    if (root.endsWith(_T('\\'))) {
      root.truncate(1);
    }
    roots.push_back(root);
  }

  *rootList = std::move(roots);
  return true;
}

bool File::mkdir() const
{
  if (CreateDirectory(m_pathName.getString(), NULL) == 0) {
    return false;
  }
  return true;
}

bool File::renameTo(const TCHAR *destPathName)
{
  File destFile(destPathName);
  return renameTo(&destFile);
}

bool File::renameTo(File *dest)
{
  StringStorage destPathName;
  dest->getPath(&destPathName);
  if (MoveFile(m_pathName.getString(), destPathName.getString()) == 0) {
    return false;
  }
  return true;
}

bool File::renameTo(const TCHAR *dest, const TCHAR *source)
{
  // Try delete the destination file. Any error will be ignored.
  DeleteFile(dest);
  if (MoveFile(source, dest) == 0) {
    return false;
  }
  return true;
}

bool File::setLastModified(INT64 time)
{
  _ASSERT(time >= 0);

  HANDLE hfile = CreateFile(m_pathName.getString(),
                            GENERIC_READ | GENERIC_WRITE,
                            0,
                            NULL,
                            OPEN_EXISTING,
                            FILE_ATTRIBUTE_NORMAL,
                            NULL);
  if (hfile == INVALID_HANDLE_VALUE) {
    return false;
  }
  ScopeExit closeFile([&] { CloseHandle(hfile); });

  //
  // Convert unix time to windows file time structure
  //

  FILETIME ft;

  DateTime dt((UINT64)time);

  dt.toFileTime(&ft);

  return SetFileTime(hfile, NULL, NULL, &ft) != FALSE;
}

bool File::truncate()
{
  if (exists()) {
    if (!remove()) {
      return false;
    }
  }
  return createNewFile();
}

bool File::getFileInfo(WIN32_FIND_DATA *fileInfo) const
{
  HANDLE fileHandle;

  //
  // Change error mode to avoid windows error message in message box
  // when we attemt to find first file on unmounted device
  //

  UINT savedErrorMode = SetErrorMode(SEM_FAILCRITICALERRORS);

  fileHandle = FindFirstFile(m_pathName.getString(), fileInfo);

  // Restore error mode
  SetErrorMode(savedErrorMode);

  if (fileHandle == INVALID_HANDLE_VALUE) {
    return false;
  } // if file exists and we get file info

  FindClose(fileHandle);
  return true;
}

bool File::tryCreateFile(DWORD desiredAccess, DWORD creationDisposition) const
{
  HANDLE hfile = CreateFile(m_pathName.getString(),
                            desiredAccess,
                            FILE_SHARE_READ,
                            NULL,
                            creationDisposition,
                            FILE_ATTRIBUTE_NORMAL | FILE_FLAG_BACKUP_SEMANTICS,
                            NULL);

  if (hfile == INVALID_HANDLE_VALUE) {
    return false;
  }

  CloseHandle(hfile);

  return true;
}
