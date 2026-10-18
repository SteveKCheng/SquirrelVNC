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

#include "Environment.h"

#include <shlobj.h>
#include <crtdbg.h>
#include <vector>

Environment::Environment()
{
}

Environment::~Environment()
{
}

void Environment::getErrStr(StringStorage *out)
{
  DWORD errCode = GetLastError();
  TCHAR buffer[1024];

  // FIXME: Remove "new line" character from buffer.
  if (FormatMessage(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS,
                    NULL, errCode,
                    MAKELANGID(LANG_ENGLISH, SUBLANG_ENGLISH_US),
                    (LPTSTR)&buffer[0],
                    sizeof(buffer), NULL) == 0) {
    out->format(_T("<<Cannot get text error describing>> (%u)"), errCode);
  } else {
    out->format(_T("%s (%u)"), buffer, errCode);
  }
}

void Environment::getErrStr(const TCHAR *specification, StringStorage *out)
{
  StringStorage sysErrText;
  getErrStr(&sysErrText);
  out->format(_T("%s (%s)"), specification, sysErrText.getString());
}

bool Environment::getSpecialFolderPath(int specialFolderId, StringStorage *out)
{
  _ASSERT(out != NULL);

  int csidl = 0;

  switch (specialFolderId) {
  case APPLICATION_DATA_SPECIAL_FOLDER:
    csidl = CSIDL_APPDATA;
    break;
  case COMMON_APPLICATION_DATA_SPECIAL_FOLDER:
    csidl = CSIDL_COMMON_APPDATA;
    break;
  default:
    _ASSERT(FALSE);
    return false;
  } // switch

  bool returnVal = false;

  TCHAR path[MAX_PATH + 1];
  if (SHGetSpecialFolderPath(NULL, &path[0], csidl, TRUE) == TRUE) {
    out->setString(&path[0]);
    returnVal = true;
  }

  return returnVal;
}

bool Environment::getCurrentModulePath(StringStorage *out)
{
  std::vector<TCHAR> buffer;
  DWORD size = MAX_PATH;

  while (true) {
    // Allocate buffer
    buffer.resize(size + 1);
    // Try to get file name
    DWORD ret = GetModuleFileName(NULL, &buffer[0], size);

    if (ret == 0) {
      return false;
    } else if (ret == size || GetLastError() == ERROR_INSUFFICIENT_BUFFER) {
      size += 128;
    } else {
      break;
    }
  } // while

  out->setString(&buffer[0]);

  return true;
} // void
