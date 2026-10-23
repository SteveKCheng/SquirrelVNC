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

#ifndef _TEXT_ENCODING_H_
#define _TEXT_ENCODING_H_

#include "util/StringStorage.h"

#include <cstddef>
#include <span>
#include <vector>

//
// Conversion between StringStorage and encoded text, as it is sent and
// received in the protocol (notes/string-encoding.md).
//
// Encoded text is typed as bytes, so that it cannot be passed where a
// TCHAR string is expected, or the other way round, by mistake. A char
// buffer that really holds encoded text is passed as
// std::as_bytes(std::span(buffer, length)).
//
// All functions:
// - Pre-conditions: the bytes are valid for the duration of the call, and
//   are not kept. Any bytes are accepted.
// - Post-conditions: decoding stops at the first zero byte, since
//   StringStorage is null-terminated; what follows it is ignored.
// - Throw Exception if the text is longer than INT_MAX bytes or
//   characters, which the Win32 conversions cannot take; the protocol's
//   limits keep real text far below that.
//
namespace TextEncoding
{
  typedef std::span<const std::byte> ByteSpan;

  //
  // Post-conditions: returns the UTF-8 bytes decoded; each invalid
  // sequence becomes U+FFFD.
  //
  StringStorage fromUtf8(ByteSpan bytes);

  //
  // Post-conditions: returns the ISO 8859-1 bytes decoded: each byte is
  // the character of the same code. Cannot fail.
  //
  StringStorage fromLatin1(ByteSpan bytes);

  //
  // Text from a server, whose encoding is not known for sure.
  //
  // Post-conditions: returns the bytes decoded as UTF-8 if they are valid
  // UTF-8, otherwise as ISO 8859-1. ASCII decodes the same either way.
  //
  StringStorage fromRfbText(ByteSpan bytes);

  //
  // Post-conditions: returns true if the bytes, up to the first zero
  // byte, are valid UTF-8 (no overlong forms, surrogates or codes above
  // U+10FFFF).
  //
  bool isValidUtf8(ByteSpan bytes);

  //
  // Post-conditions: returns text in UTF-8, without a terminator; each
  // unpaired surrogate becomes U+FFFD.
  //
  std::vector<std::byte> toUtf8(const StringStorage &text);

  //
  // Post-conditions: returns text in ISO 8859-1, without a terminator;
  // each character above U+00FF becomes '?', a surrogate pair a single
  // '?'.
  //
  std::vector<std::byte> toLatin1(const StringStorage &text);
}

#endif // _TEXT_ENCODING_H_
