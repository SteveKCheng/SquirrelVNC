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

#include "TextEncoding.h"
#include "util/Exception.h"

#include <algorithm>
#include <climits>
#include <type_traits>

// The Win32 conversions below produce WCHARs, which go into StringStorage
// as they are. A build with TCHAR = char (notes/portability.md) needs
// other implementations of the UTF-8 functions.
static_assert(std::is_same_v<TCHAR, WCHAR>, "StringStorage must hold UTF-16");

namespace
{
  //
  // Post-conditions: returns the bytes up to the first zero byte, or all
  // of them if there is none.
  // Throws Exception if that is longer than INT_MAX bytes.
  //
  TextEncoding::ByteSpan untilNull(TextEncoding::ByteSpan bytes)
  {
    auto end = std::find(bytes.begin(), bytes.end(), std::byte{0});
    TextEncoding::ByteSpan text = bytes.first(end - bytes.begin());
    if (text.size() > INT_MAX) {
      throw Exception(_T("Text is too long to convert"));
    }
    return text;
  }

  //
  // Pre-conditions: text.size() <= INT_MAX, and text has no zero byte.
  // Post-conditions: returns the text decoded with MultiByteToWideChar()
  // and flags; returns false, leaving result unchanged, if it fails
  // (with MB_ERR_INVALID_CHARS, because the text is not valid UTF-8).
  //
  bool decodeUtf8(TextEncoding::ByteSpan text, DWORD flags, StringStorage *result)
  {
    if (text.empty()) {
      result->setString(_T(""));
      return true;
    }
    const char *data = reinterpret_cast<const char *>(text.data());
    int size = static_cast<int>(text.size());
    int length = MultiByteToWideChar(CP_UTF8, flags, data, size, 0, 0);
    if (length <= 0) {
      return false;
    }
    std::vector<WCHAR> buffer(static_cast<size_t>(length) + 1);
    if (MultiByteToWideChar(CP_UTF8, flags, data, size, buffer.data(), length) != length) {
      return false;
    }
    // No zero byte in, so no null character out.
    buffer[length] = L'\0';
    result->setString(buffer.data());
    return true;
  }
}

StringStorage TextEncoding::fromUtf8(ByteSpan bytes)
{
  StringStorage result;
  if (!decodeUtf8(untilNull(bytes), 0, &result)) {
    // Without MB_ERR_INVALID_CHARS this does not happen for any input.
    throw Exception(_T("Cannot convert text from UTF-8"));
  }
  return result;
}

StringStorage TextEncoding::fromLatin1(ByteSpan bytes)
{
  ByteSpan text = untilNull(bytes);
  std::vector<WCHAR> buffer;
  buffer.reserve(text.size() + 1);
  for (std::byte b : text) {
    buffer.push_back(static_cast<WCHAR>(std::to_integer<unsigned char>(b)));
  }
  buffer.push_back(L'\0');
  return StringStorage(buffer.data());
}

StringStorage TextEncoding::fromRfbText(ByteSpan bytes)
{
  StringStorage result;
  if (decodeUtf8(untilNull(bytes), MB_ERR_INVALID_CHARS, &result)) {
    return result;
  }
  return fromLatin1(bytes);
}

bool TextEncoding::isValidUtf8(ByteSpan bytes)
{
  StringStorage ignored;
  return decodeUtf8(untilNull(bytes), MB_ERR_INVALID_CHARS, &ignored);
}

std::vector<std::byte> TextEncoding::toUtf8(const StringStorage &text)
{
  size_t length = text.getLength();
  if (length == 0) {
    return std::vector<std::byte>();
  }
  if (length > INT_MAX) {
    throw Exception(_T("Text is too long to convert"));
  }
  int size = static_cast<int>(length);
  int bytes = WideCharToMultiByte(CP_UTF8, 0, text.getString(), size, 0, 0, 0, 0);
  if (bytes <= 0) {
    throw Exception(_T("Cannot convert text to UTF-8"));
  }
  std::vector<std::byte> result(static_cast<size_t>(bytes));
  if (WideCharToMultiByte(CP_UTF8, 0, text.getString(), size,
                          reinterpret_cast<char *>(result.data()), bytes,
                          0, 0) != bytes) {
    throw Exception(_T("Cannot convert text to UTF-8"));
  }
  return result;
}

std::vector<std::byte> TextEncoding::toLatin1(const StringStorage &text)
{
  const WCHAR *chars = text.getString();
  size_t length = text.getLength();
  std::vector<std::byte> result;
  result.reserve(length);
  for (size_t i = 0; i < length; i++) {
    WCHAR c = chars[i];
    if (c <= 0xFF) {
      result.push_back(static_cast<std::byte>(c));
    } else {
      result.push_back(static_cast<std::byte>('?'));
      // A surrogate pair is one character.
      if (IS_HIGH_SURROGATE(c) && i + 1 < length && IS_LOW_SURROGATE(chars[i + 1])) {
        i++;
      }
    }
  }
  return result;
}
