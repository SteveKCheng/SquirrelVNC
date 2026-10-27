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

#include "QueuedOutputStream.h"

#include "io-lib/IOException.h"

#include <cassert>
#include <utility>

QueuedOutputStream::QueuedOutputStream()
: m_isClosed(false)
{
}

QueuedOutputStream::~QueuedOutputStream()
{
}

void QueuedOutputStream::open(std::function<void()> wake)
{
  assert(!m_wake && !m_isClosed);
  m_wake = std::move(wake);
}

size_t QueuedOutputStream::write(const void *buffer, size_t len)
{
  if (m_isClosed) {
    throw IOException(_T("The connection is closed"));
  }
  const char *bytes = static_cast<const char *>(buffer);
  m_queued.insert(m_queued.end(), bytes, bytes + len);
  if (m_wake) {
    m_wake();
  }
  return len;
}

std::vector<char> QueuedOutputStream::take()
{
  return std::exchange(m_queued, {});
}

void QueuedOutputStream::close()
{
  m_isClosed = true;
  m_wake = nullptr;
  m_queued.clear();
}
