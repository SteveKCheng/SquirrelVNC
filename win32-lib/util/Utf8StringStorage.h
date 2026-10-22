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

#ifndef __UTF8STRINGSTORAGE_H__
#define __UTF8STRINGSTORAGE_H__

#include "StringStorage.h"

// String of UTF-8 bytes, for sending and receiving strings in the protocol.
//
// As for StringStorage, the buffer always ends with a terminating null
// character, so it is never empty.  setString() adds the terminator if the
// bytes given do not end with one.  The bytes are otherwise kept as they
// are, and are not checked to be valid UTF-8 until toStringStorage().
class Utf8StringStorage
{
public:
  Utf8StringStorage();
  Utf8StringStorage(const std::vector<char> *utf8Buffer);
  Utf8StringStorage(const StringStorage *string);
  Utf8StringStorage(const Utf8StringStorage &string);
  ~Utf8StringStorage();

  // Sets this string to a copy of the bytes in utf8Buffer, which must not be
  // null, and may be empty.  A terminating null character is appended unless
  // the last byte already is one.
  void setString(const std::vector<char> *utf8Buffer);

  // Returns the bytes as a null-terminated string, never null.  The result
  // is valid only until this object is changed or destroyed.
  _Ret_z_ const char *getString() const;

  // Returns size of string in bytes, including terminating null character.
  size_t getSize() const;
  // Returns the number of bytes (not characters), not including the
  // terminating null character.
  size_t getLength() const;

  // Sets this string to src, with its terminator, converted to UTF-8.
  // @throw Exception if src has INT_MAX characters or more, which the Win32
  // conversion cannot take, or cannot be converted.  This string is then
  // unchanged.
  void fromStringStorage(const StringStorage *src);

  // Converts this string from UTF-8 and stores it in *dst.  The result ends
  // at the first null byte.  Invalid UTF-8 sequences become U+FFFD.
  // @throw Exception if this string is more than INT_MAX bytes, with its
  // terminator, or cannot be converted.  *dst is then unchanged.
  void toStringStorage(StringStorage *dst);

private:
  std::vector<char> m_buffer;
};

#endif // __UTF8STRINGSTORAGE_H__
