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
// Tests of BufferedInputStream (io-lib/BufferedInputStream.h).
//

#include "UnitTest.h"

#include "io-lib/BufferedInputStream.h"
#include "io-lib/IOException.h"

#include <algorithm>
#include <cstring>

namespace
{
  // Gives its bytes at most piece at a time, as a socket may, and counts
  // its reads. A read once the bytes are all given throws, as reading a
  // closed connection does; a socket would block instead.
  class PieceStream final : public InputStream
  {
  public:
    PieceStream(const char *data, size_t size, size_t piece)
    : m_data(data), m_size(size), m_piece(piece)
    {
    }

    size_t read(void *buffer, size_t len) override
    {
      reads++;
      if (m_pos == m_size) {
        throw IOException(_T("No more data"));
      }
      size_t count = std::min({ len, m_piece, m_size - m_pos });
      memcpy(buffer, m_data + m_pos, count);
      m_pos += count;
      return count;
    }

    size_t available() override
    {
      return 0;
    }

    int reads = 0;

  private:
    const char *m_data;
    size_t m_size;
    size_t m_piece;
    size_t m_pos = 0;
  };
}

void runBufferedInputStreamTests()
{
  char data[20];
  for (size_t i = 0; i < sizeof(data); i++) {
    data[i] = static_cast<char>(i);
  }
  PieceStream source(data, sizeof(data), 10);
  BufferedInputStream buffered(&source);

  char out[100];
  size_t first = buffered.read(out, 5);
  check(first == 5 && source.reads == 1 && out[4] == 4,
        "BufferedInputStream reads its source once for the first bytes");

  size_t rest = buffered.read(out, sizeof(out));
  check(rest == 5 && source.reads == 1 && out[0] == 5 && out[4] == 9,
        "BufferedInputStream returns the bytes it holds without reading its source again");

  size_t next = buffered.read(out, sizeof(out));
  check(next == 10 && source.reads == 2 && out[0] == 10,
        "BufferedInputStream reads its source again once it holds nothing");
}
