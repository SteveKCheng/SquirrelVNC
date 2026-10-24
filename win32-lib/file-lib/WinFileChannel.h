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

#ifndef __WINFILECHANNEL_H__
#define __WINFILECHANNEL_H__

#include "FileChannel.h"
#include "WinFile.h"

class WinFileChannel final : public FileChannel
{
public:
  // Creates a channel with opens or creates a file with desired access.
  // See WinFile for detail.
  WinFileChannel(const TCHAR *pathName, DesiredAccess dAcc, FileMode fMode,
                 bool sharedToRead = true);
  WinFileChannel(const WinFileChannel &) = delete;
  WinFileChannel &operator=(const WinFileChannel &) = delete;
  WinFileChannel(WinFileChannel &&) = delete;
  WinFileChannel &operator=(WinFileChannel &&) = delete;

  ~WinFileChannel() override;

  // Inherited from FileChannel.
  void seek(INT64 n) override;

  // Inherited from Channel.
  size_t read(void *buffer, size_t len) override;

  // Inherited from Channel.
  size_t write(const void *buffer, size_t len) override;

  // Inherited from Channel.
  void close() override;
  size_t available() override;

private:
  WinFile m_winFile;
};

#endif // __WINFILECHANNEL_H__
