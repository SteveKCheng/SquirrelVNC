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

#include "StringStorage.h"
#include "CommonHeader.h"
#include "Exception.h"
#include <stdio.h>

#include <crtdbg.h>

#include <algorithm>
#include <utility>
#include <vector>

StringStorage::StringStorage()
{
}

StringStorage::StringStorage(const TCHAR *string)
{
  setString(string);
}

StringStorage::StringStorage(const StringStorage &stringBuffer)
: m_string(stringBuffer.m_string)
{
}

StringStorage::StringStorage(StringStorage &&other) noexcept
: m_string(std::move(other.m_string))
{
  // A moved-from std::basic_string is only valid; make it empty.
  other.m_string.clear();
}

StringStorage::StringStorage(const StdString &string)
: m_string(string)
{
  cutAtNull();
}

StringStorage::StringStorage(StdString &&string)
: m_string(std::move(string))
{
  cutAtNull();
}

StringStorage::~StringStorage()
{
}

void StringStorage::cutAtNull()
{
  size_t position = m_string.find(_T('\0'));
  if (position != StdString::npos) {
    m_string.resize(position);
  }
}

void StringStorage::setString(const TCHAR *string)
{
  if (string == nullptr) {
    string = _T("");
  }
  // Copied first, as string may point into m_string.
  StdString copy(string);
  m_string = std::move(copy);
}

_Ret_z_ const TCHAR *StringStorage::getString() const
{
  return m_string.c_str();
}

const StringStorage::StdString &StringStorage::str() const &
{
  return m_string;
}

StringStorage::StdString StringStorage::str() &&
{
  StdString result = std::move(m_string);
  m_string.clear();
  return result;
}

size_t StringStorage::getLength() const
{
  return m_string.size();
}

size_t StringStorage::getSize() const
{
  return (m_string.size() + 1) * sizeof(TCHAR);
}

bool StringStorage::isEmpty() const
{
  return m_string.empty();
}

bool StringStorage::endsWith(TCHAR postfix) const
{
  return !m_string.empty() && m_string.back() == postfix;
}

bool StringStorage::beginsWith(TCHAR prefix) const
{
  return !m_string.empty() && m_string.front() == prefix;
}

void StringStorage::getSubstring(StringStorage *substr,
                                 size_t startIndex,
                                 size_t endIndex) const
{
  size_t length = getLength();
  endIndex++; // to simplify calculations; SIZE_MAX wraps to 0
  startIndex = std::min(startIndex, length);
  endIndex = std::min(endIndex, length);
  if (endIndex < startIndex) {
    endIndex = startIndex;
  }

  // startIndex <= length, so substr() does not throw. Made before the
  // assignment, as substr may be this object.
  StdString result = m_string.substr(startIndex, endIndex - startIndex);
  substr->m_string = std::move(result);
}

void StringStorage::appendString(const TCHAR *string)
{
  if (string == nullptr) {
    return;
  }

  // Copied first, as string may point into m_string, which appending may
  // reallocate.
  StdString copy(string);
  m_string += copy;
}

void StringStorage::appendString(const StringStorage& string)
{
  // operator+= on std::basic_string does not seem to allow pointing to itself
  if (this == &string)
  {
    auto len = m_string.length();
    m_string.resize(2 * len);
    m_string.copy(m_string.data(), len, len);
  }
  else
  {
    m_string += string.str();
  }
}

void StringStorage::appendChar(TCHAR c)
{
  // The null character would end the string; as appendString() of a
  // one-character string, it adds nothing.
  if (c != _T('\0')) {
    m_string.push_back(c);
  }
}

bool StringStorage::isEqualTo(const StringStorage &other) const
{
  return m_string == other.m_string;
}

bool StringStorage::isEqualTo(const TCHAR *other) const
{
  if (other == nullptr) {
    return false;
  }
  // Compares up to other's terminator; m_string has no null character, so
  // this is _tcscmp() == 0.
  return m_string == other;
}

size_t StringStorage::findChar(const TCHAR c) const
{
  return m_string.find(c);
}

size_t StringStorage::findLast(const TCHAR c) const
{
  return m_string.rfind(c);
}

void StringStorage::removeChars(const TCHAR badCharacters[], size_t count)
{
  std::erase_if(m_string, [badCharacters, count](TCHAR each) {
    return std::find(badCharacters, badCharacters + count, each) != badCharacters + count;
  });
}

void StringStorage::remove(size_t startIndex, size_t count)
{
  // Not startIndex + count > getLength(), which can wrap around.
  bool isFailed = startIndex > getLength() || count > getLength() - startIndex;
  _ASSERT(!isFailed);
  if (isFailed) {
    throw Exception(_T("An incorrect StringStorage::remove() usage"));
  }
  m_string.erase(startIndex, count);
}

void StringStorage::truncate(size_t count)
{
  count = std::min(getLength(), count);

  remove(getLength() - count, count);
}

TCHAR *StringStorage::find(const TCHAR *substr)
{
  size_t position = m_string.find(substr);
  if (position == StdString::npos) {
    return nullptr;
  }
  return m_string.data() + position;
}

size_t StringStorage::findOneOf(const TCHAR *string) const
{
  return m_string.find_first_of(string);
}

void StringStorage::toLowerCase()
{
  for (TCHAR &c : m_string) {
    if (_istalpha(c) != 0) {
      c = _totlower(c);
    }
  }
}

void StringStorage::format(_Printf_format_string_ const TCHAR *format, ...)
{
  va_list vl;

  va_start(vl, format);
  int count = _vsctprintf(format, vl);
  va_end(vl);
  if (count < 0) {
    throw Exception(_T("Cannot format string"));
  }

  std::vector<TCHAR> buffer((size_t)count + 1);
  va_start(vl, format);
  _vstprintf_s(buffer.data(), buffer.size(), format, vl);
  va_end(vl);

  // Up to the first null character, which is the terminator unless %c
  // wrote one.
  setString(buffer.data());
}

StringStorage &StringStorage::operator= (const StringStorage &other)
{
  m_string = other.m_string;
  return *this;
}

StringStorage &StringStorage::operator= (StringStorage &&other) noexcept
{
  if (this != &other) {
    m_string = std::move(other.m_string);
    other.m_string.clear();
  }
  return *this;
}

bool StringStorage::operator == (const StringStorage &str) const
{
  return isEqualTo(str);
}

bool StringStorage::operator < (const StringStorage &str) const
{
  // Neither has a null character, so this orders as _tcscmp() does.
  return m_string < str.m_string;
}

void StringStorage::operator += (const TCHAR* str)
{
  appendString(str);
}
