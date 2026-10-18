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

#ifndef _SHELL_H_
#define _SHELL_H_

#include "util/winhdr.h"

#include "SystemException.h"

/**
 * Wrapper over WinAPI shell functions.
 *
 * @author enikey.
 */
class Shell
{
public:
  /**
   * Opens file with default action specified in Windows.
   *
   * file may also be a URL, which opens in the default browser.  It should
   * be a full path or a URL: a bare file name is searched for, starting
   * with the current directory.
   *
   * As for any ShellExecute() call, the calling thread should have
   * initialized COM, since the program or shell extension that handles
   * the file may need it.
   *
   * @param file target file; must not be null.
   * @param parameters parameters, if file is a program; may be null.
   * @param workDirectory working directory; may be null for the current
   *   one.
   * @throws SystemException on fail, carrying ShellExecute()'s error code.
   *   Most of those codes are Win32 error codes, but SE_ERR_SHARE to
   *   SE_ERR_DLLNOTFOUND (26 to 32) are not, so the exception's message is
   *   wrong for them.
   */
  static void open(const TCHAR *file,
                   const TCHAR *parameters,
                   const TCHAR *workDirectory) throw(SystemException);

  /**
   * Opens a File Explorer window on the folder containing a file, with the
   * file selected.
   *
   * This goes through the shell's own API, so no process is started by
   * name: running "explorer" with CreateProcess() would search for it in
   * this program's directory first.
   *
   * The calling thread may or may not have initialized COM; if it has not,
   * COM is initialized as single-threaded for the duration of the call.
   *
   * @param file full path of the file to select; must not be null.
   * @param folder full path of the folder containing file; must not be
   *   null.  If file does not exist, folder is selected in its parent
   *   instead.
   * @throws SystemException if neither file nor folder can be found, or the
   *   shell fails to open the window.
   */
  static void showInFolder(const TCHAR *file,
                           const TCHAR *folder) throw(SystemException);
private:
  /**
   * Don't allow instanizing of class.
   */
  Shell();
};

#endif
