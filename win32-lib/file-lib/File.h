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

#ifndef _FILE_H_
#define _FILE_H_

#include "util/CommonHeader.h"
#include "util/inttypes.h"

#include <vector>

class File final
{
public:

  const static int MODE_READ  = 1;
  const static int MODE_WRITE = 2;

  File(const TCHAR *pathname);
  File(const TCHAR *parent, const TCHAR *child);
  ~File();

  //
  // Atomically creates a new, empty file named by this pathname
  // if and only if a file with this name does not yet exist.
  //

  bool createNewFile() const;

  //
  // Deletes the file or directory denoted by this pathname
  //

  bool remove() const;

  //
  // Tests whether the file or directory denoted by this pathname exists
  //

  bool exists() const;

  /**
  Returns the name of the file or directory denoted by this pathname
  (not full path to file, only filename).
  @param name out parameter for file name.
  */
  void getName(StringStorage *name) const;

  /**
  Tests whether the file denoted by this pathname is a normal file.
  @return true if file is a normal file (not directory), false otherwise.
  */
  bool isFile() const;

  /**
  Tests whether the file denoted by this abstract pathname is a directory.
  @return true if this file is directory, false otherwise.
  */
  bool isDirectory() const;

  /**
  Returns full path to file or directory.
  @param pathname out parameter for path to file or directory.
  */
  void getPath(StringStorage *pathname) const;

  /**
   * Returns the last modification time of file denoted by this pathname.
   * @return count of milliseconds since unix epoch or 0 on fail.
   * @fixme raise exception on fail.
   */
  UINT64 lastModified() const;

  /**
   * Returns the length of the file (in bytes) denoted by this pathname.
   * @fixme raise exception on fail.
   */
  UINT64 length() const;

  //
  // Post-conditions: returns true and sets *fileList to the names of the
  // files and directories in the directory this pathname denotes, other
  // than "." and ".."; returns false, leaving *fileList unchanged, if the
  // directory cannot be listed.
  //
  bool list(std::vector<StringStorage> *fileList) const;

  //
  // Post-conditions: returns true and sets *rootList to the filesystem
  // roots, as drive names without a trailing backslash ("C:"); returns
  // false, leaving *rootList unchanged, if they cannot be read.
  //
  static bool listRoots(std::vector<StringStorage> *rootList);

  //
  // Creates the directory named by this pathname
  //

  bool mkdir() const;

  //
  // Renames the file denoted by this pathname.
  // Really this methods call move method.
  //

  bool renameTo(const TCHAR *destPathName);
  bool renameTo(File *dest);
  static bool renameTo(const TCHAR *dest, const TCHAR *source);

  /**
   * Sets the last-modified time of the file or directory named by this pathname.
   * @param time count of milliseconds since unix epoch.
   * @return true if ok, false if fail.
   */

  bool setLastModified(INT64 time);

  //
  // Truncates file, removes file if it exists and creates new file with same pathname
  //

  bool truncate();

private:

  //
  // Helper methods to minimize code of File class
  //

  bool getFileInfo(WIN32_FIND_DATA *fileInfo) const;
  bool tryCreateFile(DWORD desiredAccess, DWORD creationDisposition) const;

public:
  static TCHAR s_separatorChar;

protected:
  StringStorage m_pathName;
};

#endif
