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

#include "AnsiStringStorage.h"
#include "CommonHeader.h"
#include "util/Exception.h"
#include <crtdbg.h>

AnsiStringStorage::AnsiStringStorage()
{
  setString("");
}

AnsiStringStorage::AnsiStringStorage(const char *string)
{
  setString(string);
}

AnsiStringStorage::AnsiStringStorage(const StringStorage *string)
{
  fromStringStorage(string);
}

AnsiStringStorage::AnsiStringStorage(const AnsiStringStorage &stringBuffer)
{
  *this = stringBuffer;
}

AnsiStringStorage::~AnsiStringStorage()
{
}

void AnsiStringStorage::setString(const char *string)
{
  if (string == 0) {
    string = "";
  }
  size_t length = strlen(string);
  if (length == SIZE_MAX)
    throw Exception(_T("String too large"));

  m_buffer.resize(length + 1);
  // memmove because string may point into this object's own buffer, as in
  // self-assignment; shrinking the vector does not reallocate it.
  memmove(m_buffer.data(), string, getSize());
}

const char *AnsiStringStorage::getString() const
{
  return m_buffer.data();
}

size_t AnsiStringStorage::getLength() const
{
  return m_buffer.size() - 1;
}

size_t AnsiStringStorage::getSize() const
{
  return m_buffer.size() * sizeof(char);
}

bool AnsiStringStorage::isEmpty() const
{
  return getLength() == 0;
}

void AnsiStringStorage::fromStringStorage(const StringStorage *src)
{
#ifndef _UNICODE
  setString(src->getString());
#else
  // WideCharToMultiByte takes and returns lengths as int.  Pass the length
  // of the source explicitly, including its terminating null character, so
  // that the result includes the terminator too.
  size_t srcLength = src->getLength();
  if (srcLength >= INT_MAX)
    throw Exception(_T("String too large for conversion"));
  int srcCount = (int)srcLength + 1;

  // Fails, returning 0, if the result would not fit in an int.
  int symbolCount = WideCharToMultiByte(CP_ACP, 0, src->getString(), srcCount,
                                        NULL, 0, NULL, NULL);
  if (symbolCount == 0) {
    throw Exception(_T("Cannot convert from StringStorage to Ansi"));
  }

  // Convert into a separate buffer so that this string is left unchanged
  // if the conversion fails.
  BufferType buffer(symbolCount);
  if (WideCharToMultiByte(CP_ACP, 0, src->getString(), srcCount,
                          buffer.data(), symbolCount, NULL, NULL) == 0) {
    throw Exception(_T("Cannot convert from StringStorage to Ansi"));
  }
  m_buffer.swap(buffer);
#endif
}

void AnsiStringStorage::toStringStorage(StringStorage *dst) const
{
#ifndef _UNICODE
  dst->setString(getString());
#else
  size_t rawSize = getSize();
  const size_t MAX_SAFE_SIZE = INT_MAX - 1024;
  if (rawSize > MAX_SAFE_SIZE)
    throw Exception(_T("String too large for conversion"));

  int symbolCount = (int)rawSize;

  std::vector<WCHAR> unicodeBuffer(symbolCount);
  int result = MultiByteToWideChar(CP_ACP, 0, m_buffer.data(),
                                   symbolCount,
                                   unicodeBuffer.data(),
                                   symbolCount);
  if (result == 0) {
    throw Exception(_T("Cannot convert from Ansi to StringStorage"));
  }

  dst->setString(unicodeBuffer.data());
#endif
}

void AnsiStringStorage::format(const char *format, ...)
{
  va_list vl;

  va_start(vl, format);
  int count = _vscprintf(format, vl);
  va_end(vl);
  if (count < 0) {
    throw Exception(_T("Cannot format string"));
  }

  m_buffer.resize((size_t)count + 1);

  va_start(vl, format);
  vsprintf_s(m_buffer.data(), (size_t)count + 1, format, vl);
  va_end(vl);
}

void AnsiStringStorage::appendString(const char *string)
{
  if (string == 0) {
    return;
  }
  AnsiStringStorage src(string);

  BufferType::iterator to = m_buffer.begin() + getLength();
  BufferType::iterator fromFirst = src.m_buffer.begin();
  BufferType::iterator fromLast = src.m_buffer.begin() + src.getLength();

  m_buffer.insert(to, fromFirst, fromLast);
}