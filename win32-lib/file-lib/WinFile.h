// Copyright (C) 2010,2011,2012 GlavSoft LLC.
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

#ifndef __WINFILE_H__
#define __WINFILE_H__

#include "util/CommonHeader.h"

enum DesiredAccess
{
  F_READ,
  F_WRITE,
  F_READ_WRITE
};

enum FileMode
{
  // Opens a file, always.
  // If the specified file does not exist and is a valid path to a writable
  // location, a function creates a file.
  FM_APPEND,
  // Creates a new file, always.
  // If the specified file exists and is writable, a function overwrites
  // the file, a function succeeds.
  FM_CREATE,
  // Creates a new file, only if it does not already exist.
  // If the specified file exists, a function fails with an Exception.
  FM_CREATE_NEW,
  // Opens a file, only if it exists.
  // If the specified file does not exist, a function fails with an Exception.
  FM_OPEN,
  // Opens a file, always.
  // If the specified file does not exist and is a valid path to a writable
  // location, a function creates a file.
  FM_OPEN_OR_CREATE,
  // Opens a file and truncates it so that its size is zero bytes,
  // only if it exists.
  // If the specified file does not exist, a function fails.
  FM_TRUNCATE
};

class WinFile
{
public:
  WinFile(const TCHAR *pathToFile, DesiredAccess dAcc, FileMode fMode,
          bool shareToRead = false);
  WinFile();

  WinFile(const WinFile &) = delete;
  WinFile &operator=(const WinFile &) = delete;
  WinFile(WinFile &&) = delete;
  WinFile &operator=(WinFile &&) = delete;

  ~WinFile();

  // Opens or creates pathToFile, as fMode says, for the access dAcc.  The
  // file is shared with other openers for reading only if shareToRead is
  // true, and never for writing.  The constructor taking the same
  // arguments calls this.
  //
  // Pre: no file is open: isValid() is false, as after the default
  // constructor or close().  Otherwise the open handle leaks.
  // Pre: dAcc and fMode are values of their enumerations; otherwise
  // SystemException is thrown with ERROR_INVALID_PARAMETER (after an
  // assertion failure in Debug builds).
  //
  // Post: isValid() is true, and the file pointer is at the start of the
  // file, or at its end for FM_APPEND.
  // @throw SystemException if the file cannot be opened, in which case
  // isValid() is false; or if FM_APPEND cannot move to the end of the
  // file, in which case the file is left open.
  void open(const TCHAR *pathToFile, DesiredAccess dAcc, FileMode fMode,
            bool shareToRead = false);

  // Closes handle to a file if handle is valid.
  void close();

  // @throw exception on a system error.
  size_t read(void *buff, size_t count);

  // @throw exception on a system error.
  size_t write(const void *buff, size_t count);

  // Return true if object is valid. The object will be valid if
  // after by default constructor creation the initialize function has been
  // called successfully.
  bool isValid();

  // Set file pointer to specified position starting from current
  // file pointer position. Can move forward and backward.
  void seek(INT64 n);

  // @throw exception on a system error.
  void flush();

  // Returns the number of bytes from the file pointer to the end of the
  // file: 0 if the pointer is at or past the end, and SIZE_MAX if the count
  // does not fit in a size_t (files over 4 GiB on Win32).
  // @throw IOException on a system error.
  size_t available();

private:
  HANDLE m_hFile;
  StringStorage m_pathToFile;
};

#endif // __WINFILE_H__
