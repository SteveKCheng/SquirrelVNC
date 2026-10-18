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

#ifndef __ANSISTRINGSTORAGE_H__
#define __ANSISTRINGSTORAGE_H__

#include "StringStorage.h"

class AnsiStringStorage
{
public:
  AnsiStringStorage();
  AnsiStringStorage(const char *string);
  AnsiStringStorage(const StringStorage *string);
  AnsiStringStorage(const AnsiStringStorage &stringBuffer);
  ~AnsiStringStorage();

  // Fills internal buffer by the string.
  virtual void setString(const char *string);

  // Returns the characters as a null-terminated string, never null.  As for
  // StringStorage, the buffer always holds the terminating null character,
  // and the result is valid only until this object is changed or destroyed.
  _Ret_z_ const char *getString() const;

    // Returns length of string (in characters, not including terminating null character).
  size_t getLength() const;

  // Returns size of string in bytes, including terminating null character.
  size_t getSize() const;

  // Checks if string is empty.
  // @return true if string is empty.
  bool isEmpty() const;

  /**
   * Sets this string to src converted to the system's ANSI code page.
   *
   * Characters that the code page cannot represent are replaced by its
   * default character, as WideCharToMultiByte does.
   *
   * @param src the string to convert; must not be null.
   * @throws Exception if src is too long for the Windows conversion
   *   functions (INT_MAX characters or more), or the conversion fails.
   *   This string is then left unchanged.
   */
  void fromStringStorage(const StringStorage *src);
  void toStringStorage(StringStorage *dst) const;

  /**
   * Sets this string to the result of formatting with printf-style
   * arguments.
   *
   * @param format the format string, which must not contain text from
   *   outside the program: pass any such text as an argument for %s.  The
   *   arguments must match it as for printf; /analyze checks this.
   * @throws Exception if the CRT cannot format the string, for example
   *   because of an invalid format specification.  This string is then
   *   left unchanged.
   */
  void format(_Printf_format_string_ const char *format, ...);

  void appendString(const char *string);

private:
  typedef std::vector<char> BufferType;
  BufferType m_buffer;
};

#endif // __ANSISTRINGSTORAGE_H__
