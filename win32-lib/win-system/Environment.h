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

#ifndef _ENVIRONMENT_H_
#define _ENVIRONMENT_H_

#include "util/StringStorage.h"

class Environment final
{
public:
  static const int APPLICATION_DATA_SPECIAL_FOLDER = 0x0; 
  static const int COMMON_APPLICATION_DATA_SPECIAL_FOLDER = 0x1;
public:
  Environment();
  ~Environment();

  // Sets out to the system's description of the error in GetLastError(),
  // followed by the error code in parentheses.  If the system has no
  // description, out is set to a placeholder with the code instead.
  // out must not be null.  Does not throw, other than on lack of memory.
  static void getErrStr(StringStorage *out);

  // Formates string from last system error code with specification before.
  // @return StringStorage object that contain error describing with
  // secificatioin.
  static void getErrStr(const TCHAR *specification, StringStorage *out);

  //
  // Sets path to special folder value to out argument.
  //
  // Valid values for specialFolderId argument are:
  //  - Environment::APPLICATION_DATA_SPECIAL_FOLDER
  //  - Environment::COMMON_APPLICATION_DATA_SPECIAL_FOLDER
  //
  // Remark: if caller set invalid value into specialFolderId assertion
  // will be failed.
  //

  static bool getSpecialFolderPath(int specialFolderId, StringStorage *out);

  // Sets full path to current executing process file to out argument
  static bool getCurrentModulePath(StringStorage *out);
};

#endif
