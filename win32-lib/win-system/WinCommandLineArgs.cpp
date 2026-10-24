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

#include "WinCommandLineArgs.h"
#include "util/Exception.h"
#include "util/ScopeExit.h"

WinCommandLineArgs::WinCommandLineArgs(const TCHAR *cmdLineInWinFormat)
{
  // StringStorage holds UTF-16, which CommandLineToArgvW() takes.
  StringStorage cmdLine(cmdLineInWinFormat);
  size_t cmdLen = cmdLine.getLength();
  if (cmdLen > 0) {
    int nArgs;
    LPWSTR *argList = CommandLineToArgvW(cmdLine.getString(), &nArgs);
    if(argList == nullptr) {
      throw Exception(_T("Invalid command line"));
    }
    ScopeExit freeArgList([&] { LocalFree(argList); });
    for(int i = 0; i < nArgs; i++) {
      StringStorage arg(argList[i]);
      if (arg.getLength() > 0) {
        m_args.push_back(arg);
      }
    }
  }
}

WinCommandLineArgs::~WinCommandLineArgs()
{
}
