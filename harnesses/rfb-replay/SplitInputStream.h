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

#ifndef _SPLIT_INPUT_STREAM_H_
#define _SPLIT_INPUT_STREAM_H_

#include "io-lib/ByteArrayInputStream.h"

#include <cstdint>

//
// An InputStream over bytes in memory that gives them in pieces, as a
// network connection may: each read() returns at most maxPiece bytes, or,
// with a seed, a random number of bytes from 1 to maxPiece. So the
// protocol code is suspended and resumed at many different points of the
// same stream (notes/async-networking.md, Phase 3, "Test").
//
// Throws as ByteArrayInputStream does: IOException at the end of the data.
//
class SplitInputStream final : public InputStream
{
public:
  //
  // Pre-conditions: buffer points to bufferSize bytes, which outlive this
  // object; maxPiece is at least 1.
  //
  SplitInputStream(const char *buffer, size_t bufferSize, size_t maxPiece);

  //
  // As above, with pieces of random sizes from 1 to maxPiece, chosen by a
  // generator seeded with seed, so that a run can be repeated. The
  // generator is a xorshift of our own, so that a seed gives the same
  // pieces with any compiler and standard library.
  //
  SplitInputStream(const char *buffer, size_t bufferSize, size_t maxPiece,
                   unsigned int seed);

  ~SplitInputStream() override;

  size_t read(void *buffer, size_t len) override;
  size_t available() override;

private:
  ByteArrayInputStream m_stream;
  size_t m_maxPiece;
  bool m_isRandom;
  // xorshift32; never 0.
  uint32_t m_state;
};

#endif // _SPLIT_INPUT_STREAM_H_
