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

#ifndef _MEMORY_OUTPUT_STREAM_H_
#define _MEMORY_OUTPUT_STREAM_H_

#include "io-lib/OutputStream.h"
#include "thread/LocalMutex.h"

#include <vector>

//
// An OutputStream that collects everything written to it in memory.
//
class MemoryOutputStream : public OutputStream
{
public:
  MemoryOutputStream();
  virtual ~MemoryOutputStream();

  //
  // Pre-conditions: buffer points to len bytes.
  // Post-conditions: the bytes are appended; returns len.
  //
  virtual size_t write(const void *buffer, size_t len);

  //
  // Post-conditions: returns a copy of everything written so far.
  //
  std::vector<char> getData();

private:
  LocalMutex m_lock;
  std::vector<char> m_data;
};

#endif // _MEMORY_OUTPUT_STREAM_H_
