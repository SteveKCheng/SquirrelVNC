// Copyright (C) 2012 GlavSoft LLC.
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

#include "LogWriter.h"
#include <cstdarg>
#include <vector>

LogWriter::LogWriter(Logger *logger)
: m_logger(logger)
{
}

LogWriter::~LogWriter()
{
}

void LogWriter::interror(_Printf_format_string_ const TCHAR *fmt, ...)
{
  int level = LOG_INTERR;
  if (m_logger != nullptr && m_logger->acceptsLevel(level)) {
    va_list vl;
    va_start(vl, fmt);
    vprintLog(level, fmt, vl);
    va_end(vl);
  }
}

void LogWriter::error(_Printf_format_string_ const TCHAR *fmt, ...)
{
  int level = LOG_ERR;
  if (m_logger != nullptr && m_logger->acceptsLevel(level)) {
    va_list vl;
    va_start(vl, fmt);
    vprintLog(level, fmt, vl);
    va_end(vl);
  }
}

void LogWriter::warning(_Printf_format_string_ const TCHAR *fmt, ...)
{
  int level = LOG_WARN;
  if (m_logger != nullptr && m_logger->acceptsLevel(level)) {
    va_list vl;
    va_start(vl, fmt);
    vprintLog(level, fmt, vl);
    va_end(vl);
  }
}

void LogWriter::message(_Printf_format_string_ const TCHAR *fmt, ...)
{
  int level = LOG_MSG;
  if (m_logger != nullptr && m_logger->acceptsLevel(level)) {
    va_list vl;
    va_start(vl, fmt);
    vprintLog(level, fmt, vl);
    va_end(vl);
  }
}

void LogWriter::info(_Printf_format_string_ const TCHAR *fmt, ...)
{
  int level = LOG_INFO;
  if (m_logger != nullptr && m_logger->acceptsLevel(level)) {
    va_list vl;
    va_start(vl, fmt);
    vprintLog(level, fmt, vl);
    va_end(vl);
  }
}

void LogWriter::detail(_Printf_format_string_ const TCHAR *fmt, ...)
{
  int level = LOG_DETAIL;
  if (m_logger != nullptr && m_logger->acceptsLevel(level)) {
    va_list vl;
    va_start(vl, fmt);
    vprintLog(level, fmt, vl);
    va_end(vl);
  }
}

void LogWriter::debug(_Printf_format_string_ const TCHAR *fmt, ...)
{
  int level = LOG_DEBUG;
  if (m_logger != nullptr && m_logger->acceptsLevel(level)) {
    va_list vl;
    va_start(vl, fmt);
    vprintLog(level, fmt, vl);
    va_end(vl);
  }
}

// Turn off the MSVC warning about "unsafe functions" in the CRT.
// 
// The below usage is safe since the required buffer size is calculated upfront,
// exactly with a count-only version of "printf".
#pragma warning(push)
#pragma warning(disable:4996)   

void LogWriter::vprintLog(int logLevel, const TCHAR *fmt, va_list argList)
{
  if (m_logger != nullptr) {
    // Format the original string.  argList is read twice, to count and then
    // to format, so the count reads a copy: reading a va_list leaves it
    // unusable on some ABIs (MSVC's is a plain pointer passed by value).
    va_list countArgs;
    va_copy(countArgs, argList);
    int count = _vsctprintf(fmt, countArgs);
    va_end(countArgs);
    std::vector<TCHAR> formattedString;
    if (count >= 0) {
      formattedString.resize((size_t)count + 1);
      _vstprintf(formattedString.data(), fmt, argList);
      m_logger->print(logLevel, formattedString.data());
    } else {
      // The CRT could not format the message.  Log the format string as is,
      // so the message is not lost.
      m_logger->print(logLevel, fmt);
    }
  }
}

#pragma warning(pop)    // disable:C4996
