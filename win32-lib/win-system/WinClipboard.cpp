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

#include "WinClipboard.h"
#include "util/ScopeExit.h"

#include <vector>

WinClipboard::WinClipboard(HWND hwnd) 
  : m_hWnd(hwnd)
{
}

WinClipboard::~WinClipboard() 
{
  // Nothing to free: text put on the clipboard belongs to the system.
}

void WinClipboard::setHWnd(HWND hwnd)
{
  m_hWnd = hwnd;
  SetClipboardViewer(m_hWnd);
}

bool WinClipboard::getString(StringStorage *str)
{
  UINT strType = CF_UNICODETEXT;

  if (sizeof(TCHAR) == 1) {
    strType = CF_TEXT;
  }
  // The size is the number of formats in the list.
  int uFormat = GetPriorityClipboardFormat(&strType, 1);

  if (uFormat == 0 || uFormat == -1) {
     return false;
  }
  if (!OpenClipboard(m_hWnd)) {
    return false;
  }
  ScopeExit closeClipboard([&] { CloseClipboard(); });
  HANDLE hndData = GetClipboardData(uFormat);
  if (hndData == 0) {
    return false;
  }
  const TCHAR *data = static_cast<const TCHAR *>(GlobalLock(hndData));
  if (data == 0) {
    return false;
  }
  ScopeExit unlockData([&] { GlobalUnlock(hndData); });
  // The text should end with a null character, but whoever put it there
  // may not have done so: read no further than its memory.
  size_t length = _tcsnlen(data, GlobalSize(hndData) / sizeof(TCHAR));
  StringStorage nativeClipboard(StringStorage::StdString(data, length));
  *str = removeCR(&nativeClipboard);
  return true;
}

bool WinClipboard::setString(const StringStorage *serverClipboard)
{
  StringStorage nativeClipboard = addCR(serverClipboard);

  int dataType = CF_UNICODETEXT;

  if (sizeof(TCHAR) == 1) {
     dataType = CF_TEXT;
  }
  // With the terminating null character.
  size_t dataSize = (nativeClipboard.getLength() + 1) * sizeof(TCHAR);

  HGLOBAL hndData = GlobalAlloc(GMEM_MOVEABLE, dataSize);
  if (hndData == 0) {
    return false;
  }
  // Released once SetClipboardData() succeeds: the memory then belongs to
  // the system, which frees it when the clipboard is next emptied.
  ScopeExit freeData([&] { GlobalFree(hndData); });
  TCHAR *dst = static_cast<TCHAR *>(GlobalLock(hndData));
  if (dst == 0) {
    return false;
  }
  CopyMemory(dst, nativeClipboard.getString(), dataSize);
  GlobalUnlock(hndData);

  if (!OpenClipboard(m_hWnd)) {
    return false;
  }
  ScopeExit closeClipboard([&] { CloseClipboard(); });
  // EmptyClipboard() makes m_hWnd the owner, which SetClipboardData()
  // needs, and removes the other formats of what was there before.
  if (!EmptyClipboard() || SetClipboardData(dataType, hndData) == 0) {
    return false;
  }
  freeData.release();
  return true;
}

StringStorage WinClipboard::addCR(const StringStorage *str)
{
  const TCHAR *beginString = str->getString();
  const TCHAR *endString = beginString + str->getLength() + 1; // start + lenght + '\0'
  std::vector<TCHAR> chars(beginString, endString);
  std::vector<TCHAR> newChars(str->getLength() * 2 + 1);
  size_t countLF = 0;
  for (size_t i = 0; i < chars.size(); i++) {
    // if is first byte or previous byte not CR, then add CR
    if ((i == 0 || chars[i-1] != CR) && chars[i] == LF) {
      newChars[i + countLF] = CR;
      ++countLF;
    }
    newChars[i + countLF] = chars[i];
  }
  newChars.resize(chars.size() + countLF);
  return StringStorage(&newChars.front());
}

StringStorage WinClipboard::removeCR(const StringStorage *str)
{
  const TCHAR *beginString = str->getString();
  const TCHAR *endString = beginString + str->getLength() + 1; // start + lenght + '\0'
  std::vector<TCHAR> chars(beginString, endString);
  std::vector<TCHAR> newChars;
  for (size_t i = 0; i < chars.size(); i++) {
    if (chars[i] != CR || i + 1 == chars.size() || chars[i+1] != LF) {
      newChars.push_back(chars[i]);
    }
  }
  return StringStorage(&newChars.front());
}
