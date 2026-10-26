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

#include "SplitInputStream.h"

#include <algorithm>

SplitInputStream::SplitInputStream(const char *buffer, size_t bufferSize,
                                   size_t maxPiece)
: m_stream(buffer, bufferSize),
  m_maxPiece(maxPiece),
  m_isRandom(false),
  m_state(1)
{
}

SplitInputStream::SplitInputStream(const char *buffer, size_t bufferSize,
                                   size_t maxPiece, unsigned int seed)
: m_stream(buffer, bufferSize),
  m_maxPiece(maxPiece),
  m_isRandom(true),
  m_state(seed != 0 ? seed : 1)
{
}

SplitInputStream::~SplitInputStream()
{
}

size_t SplitInputStream::read(void *buffer, size_t len)
{
  size_t piece = m_maxPiece;
  if (m_isRandom) {
    m_state ^= m_state << 13;
    m_state ^= m_state >> 17;
    m_state ^= m_state << 5;
    piece = 1 + m_state % m_maxPiece;
  }
  return m_stream.read(buffer, std::min(len, piece));
}

size_t SplitInputStream::available()
{
  return std::min(m_stream.available(), m_maxPiece);
}
