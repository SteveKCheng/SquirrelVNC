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

#ifndef _STRING_BUFFER_H_
#define _STRING_BUFFER_H_

#include "winhdr.h"
#include <vector>

/**
 * String of TCHAR, owning its characters.
 *
 * The buffer always holds the characters followed by a terminating null
 * character, so it is never empty, even for the empty string.  Every member
 * function keeps this so: those that can fail throw and leave the string
 * unchanged.  getString() therefore always returns a valid, null-terminated
 * string, and getLength() is one less than the number of elements.
 */
class StringStorage
{
public:
  /**
  Creates new string storage with unset string (zero string).
  */
  StringStorage();

  /**
  Creates new string storage with specified string that will be
  copied to buffer of this string storage.
  @param string string to copy.
  */
  StringStorage(const TCHAR *string);

  /**
  Copy constructor.
  */
  StringStorage(const StringStorage &stringBuffer);

  /**
  Class destructor.
  */
  virtual ~StringStorage();

  // Fills internal buffer by the string.
  virtual void setString(const TCHAR *string);

  /**
   * Returns the characters as a null-terminated string.
   *
   * The result is never null.  It points into this object's buffer, so it is
   * valid only until this object is changed or destroyed.
   */
  _Ret_z_ const TCHAR *getString() const;

    // Returns length of string (in characters, not including terminating null character).
  size_t getLength() const;

  // Returns size of string in bytes, including terminating null character.
  size_t getSize() const;

  // Checks if string is empty.
  // @return true if string is empty.
  bool isEmpty() const;

  /**
   * Tests if string ends with specified character.
   * @param postfix char to test.
   * @return true if string ends with postfix.
   */
  bool endsWith(TCHAR postfix) const;

  /**
   * Tests if string begins with specified character.
   * @param prefix char to test.
   * @return true if string begins with prefix.
   */
  bool beginsWith(TCHAR prefix) const;

  /**
   * Copies a substring specified by indexes of first and last characters into
   * another StringStorage object. endIndex is included, so the maximum length
   * can be calculated as (endIndex - startIndex + 1). The destination may be
   * the same object as the source.
   */
  // Returns a new string that is a getSubstring of this string
  void getSubstring(StringStorage *substr, size_t startIndex,
                    size_t endIndex) const;

  /**
   * Adds string to the end of this string.
   * @param string string to be added.
   */
  void appendString(const TCHAR *string);

  /**
   * Adds char to the end of this string.
   * @fixme rename string method to append or appendChar.
   * @param c char to be added.
   */
  void appendChar(TCHAR c);

  /**
   * Checks if this string are equals to other.
   * @param other other string.
   * @return true if strings are equal.
   * @remark if both strings are 0 (not set) then returns true.
   */
  bool isEqualTo(const TCHAR *other) const;

  /**
   * Checks if string contained by this storage equals to string in other storage.
   * @remark see overload of isEqualTo method.
   */
  bool isEqualTo(const StringStorage *other) const;

  //
  // Return a pointer to the first occurrence of a search substr in the
  // internall string or
  // zero otherwise.
  //
  TCHAR *find(const TCHAR *substr);

  //
  // Returns first position of character in string if found,
  // -1 otherwise.
  //

  size_t findChar(const TCHAR c);

  //
  // Returns first position of any character that string argument have.
  //

  size_t findOneOf(const TCHAR *string);

  /**
   * Finds last position of specified character in this string.
   * @param c character to find.
   * @return last position of character in this string or -1 if not found.
   */
  size_t findLast(const TCHAR c);

  /**
   * Removes characters from string.
   * @param badCharacters array of characters to remove from string.
   * @param count count of characters in badCharacters array.
   */
  void removeChars(const TCHAR badCharacters[], size_t count);

  // Removes count characters from the specified start index.
  // Throws Exception if startIndex + count > length in the release
  // configuration and throws an ASSERTION FAILED message in the debug
  // configuration.
  void remove(size_t startIndex, size_t count);

  /**
   * Truncates string.
   * @param count count of characters to remove from the end of string.
   */
  void truncate(size_t count);

  //
  // Converts string notated by this container to lower case
  //

  void toLowerCase();

  /**
   * Sets this string to the result of formatting with printf-style
   * arguments.
   *
   * @param format the format string, which must not contain text from
   *   outside the program, such as text from the server or the message of an
   *   exception: pass any such text as an argument for %s.  The arguments
   *   must match it as for printf; the code analyser (/analyze) checks this.
   * @throws Exception if the CRT cannot format the string.  This string is
   *   then left unchanged.
   */
  void format(_Printf_format_string_ const TCHAR *format, ...);

  //
  // Overload operator = for container copying
  //

  void operator = (const StringStorage &other);

  bool operator == (const StringStorage &str) const;
  bool operator < (const StringStorage &str) const;
  void operator += (const TCHAR* str);

private:
  typedef std::vector<TCHAR> BufferType;
  BufferType m_buffer;

  void resizeBuffer(size_t newSize);

  // This simplify to read the buffer string
#ifdef _DEBUG
  TCHAR *m_readableString;
#endif
};

#endif
