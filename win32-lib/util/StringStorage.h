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

#ifndef _STRING_BUFFER_H_
#define _STRING_BUFFER_H_

#include "winhdr.h"
#include <string>

/**
 * String of TCHAR, owning its characters: UTF-16 on Windows.
 *
 * The characters are kept in a std::basic_string<TCHAR>.  A StringStorage
 * never contains a null character: every way of setting or adding text
 * takes a null-terminated string, or stops at the first null character, so
 * getString() and getLength() always describe the same text, and the
 * comparisons give the same results as the C string functions would.
 * Members that can fail throw and leave the string unchanged, unless they
 * say otherwise.
 *
 * Moving from a StringStorage leaves it empty.
 *
 * For code that needs the standard string, such as a library's interface,
 * str() gives the characters as a StdString, and the StdString
 * constructors take one; both move when given an rvalue.
 */
class StringStorage final
{
public:
  typedef std::basic_string<TCHAR> StdString;

  /**
  Creates new string storage with unset string (zero string).
  */
  StringStorage();

  /**
  Creates new string storage with specified string that will be
  copied to buffer of this string storage.
  @param string string to copy; null for the empty string.
  */
  StringStorage(const TCHAR *string);

  /**
  Copy constructor.
  */
  StringStorage(const StringStorage &stringBuffer);

  /**
   * Move constructor.  Post-condition: other is empty.
   */
  StringStorage(StringStorage &&other) noexcept;

  /**
   * Creates a string from a standard one, copied or moved.
   *
   * Post-condition: holds the characters of string up to its first null
   * character, if it has one, since a StringStorage never contains one.
   */
  explicit StringStorage(const StdString &string);
  explicit StringStorage(StdString &&string);

  ~StringStorage();

  // Sets this string to a copy of string, which may be null for the empty
  // string.  string may point into this object's own characters (but is
  // then invalidated, as for any change).
  void setString(const TCHAR *string);

  /**
   * Returns the characters as a null-terminated string.
   *
   * The result is never null.  It points into this object's buffer, so it is
   * valid only until this object is changed or destroyed.
   */
  _Ret_z_ const TCHAR *getString() const;

  /**
   * Returns the characters as a standard string, which has no null
   * character inside.
   *
   * On an lvalue the result refers to this object's string, valid until
   * this object is changed or destroyed. On an rvalue, as in
   * std::move(s).str(), the characters are moved out and s is left empty.
   */
  const StdString &str() const &;
  StdString str() &&;

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
   * @return true if string ends with postfix; false if it is empty.
   */
  bool endsWith(TCHAR postfix) const;

  /**
   * Tests if string begins with specified character.
   * @param prefix char to test.
   * @return true if string begins with prefix; false if it is empty.
   */
  bool beginsWith(TCHAR prefix) const;

  /**
   * Copies a substring specified by indexes of first and last characters into
   * another StringStorage object. endIndex is included, so the maximum length
   * can be calculated as (endIndex - startIndex + 1). The destination may be
   * the same object as the source.
   *
   * Unlike std::basic_string::substr(), this never throws for indexes out of
   * range: both are clamped to the length, and an end before the start gives
   * the empty string.  endIndex SIZE_MAX gives the empty string too, as
   * endIndex + 1 wraps to 0; callers that pass getLength() - 1 or
   * getLength() - 2 for a short string rely on that.
   */
  void getSubstring(StringStorage *substr, size_t startIndex,
                    size_t endIndex) const;

  /**
   * Adds string to the end of this string.
   * @param string string to be added; null adds nothing.  It may point into
   *   this object's own characters.
   */
  void appendString(const TCHAR *string);

  /**
   * Adds string to the end of this string.
   * @param string string to be added
   */
  void appendString(const StringStorage& string);

  /**
   * Adds char to the end of this string.
   * @fixme rename string method to append or appendChar.
   * @param c char to be added; the null character adds nothing.
   */
  void appendChar(TCHAR c);

  /**
   * Checks if this string are equals to other.
   * @param other other string.
   * @return true if strings are equal; false if other is null.
   */
  bool isEqualTo(const TCHAR *other) const;

  /**
   * Checks if string contained by this storage equals to string in other storage.
   */
  bool isEqualTo(const StringStorage &other) const;

  //
  // Return a pointer to the first occurrence of a search substr in the
  // internal string, or zero otherwise. An empty substr is found at the
  // start. The pointer is valid until this string is changed or destroyed;
  // characters may be changed through it, but not to the null character.
  // Pre-conditions: substr is not null.
  //
  TCHAR *find(const TCHAR *substr);

  //
  // Returns first position of character in string if found,
  // -1 otherwise (always for the null character).
  //

  size_t findChar(const TCHAR c) const;

  //
  // Returns first position of any character that string argument have,
  // -1 otherwise.
  // Pre-conditions: string is not null.
  //

  size_t findOneOf(const TCHAR *string) const;

  /**
   * Finds last position of specified character in this string.
   * @param c character to find.
   * @return last position of character in this string or -1 if not found.
   */
  size_t findLast(const TCHAR c) const;

  /**
   * Removes characters from string.
   * @param badCharacters array of characters to remove from string.
   * @param count count of characters in badCharacters array.
   */
  void removeChars(const TCHAR badCharacters[], size_t count);

  // Removes count characters from the specified start index.  The string is
  // unchanged if this throws.
  // Throws Exception if startIndex + count > length (computed without
  // overflow) in the release configuration and throws an ASSERTION FAILED
  // message in the debug configuration.  (std::basic_string::erase()
  // instead clamps count.)
  void remove(size_t startIndex, size_t count);

  /**
   * Truncates string.
   * @param count count of characters to remove from the end of string; at
   *   most the whole string is removed.
   */
  void truncate(size_t count);

  //
  // Converts string notated by this container to lower case, as the C
  // runtime's current locale converts each character (_totlower()).
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
   *   If the result contains a null character (%c with 0), the string ends
   *   before it.
   * @throws Exception if the CRT cannot format the string.  This string is
   *   then left unchanged.
   */
  void format(_Printf_format_string_ const TCHAR *format, ...);

  StringStorage &operator = (const StringStorage &other);

  // Post-condition: other is empty, unless it is this object.
  StringStorage &operator = (StringStorage &&other) noexcept;

  bool operator == (const StringStorage &str) const;
  bool operator < (const StringStorage &str) const;
  void operator += (const TCHAR* str);

private:
  // Post-condition: m_string ends before its first null character.
  void cutAtNull();

  StdString m_string;
};

#endif
