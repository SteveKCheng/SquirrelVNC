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

#include "Shell.h"

#include <shlobj.h>

Shell::Shell()
{
}

void Shell::open(const TCHAR *file, const TCHAR *parameters, const TCHAR *workDirectory)
{
  // ShellExecute() returns an HINSTANCE only for compatibility with 16-bit
  // Windows; as documented, it is cast to INT_PTR, and 32 or less is an
  // error code.
  INT_PTR ret = reinterpret_cast<INT_PTR>(
    ShellExecute(0, _T("open"), file, parameters, workDirectory, SW_SHOW));

  if (ret <= 32) {
    throw SystemException(static_cast<int>(ret));
  }
}

void Shell::showInFolder(const TCHAR *file, const TCHAR *folder)
{
  // SHOpenFolderAndSelectItems() requires COM.  S_FALSE means it was already
  // initialized on this thread, which still needs a matching CoUninitialize;
  // RPC_E_CHANGED_MODE means it was initialized as multithreaded, which the
  // shell also accepts, and must not be uninitialized here.
  HRESULT hrInit = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED |
                                        COINIT_DISABLE_OLE1DDE);
  if (FAILED(hrInit) && hrInit != RPC_E_CHANGED_MODE) {
    throw SystemException(hrInit);
  }

  // Fails if the path does not exist.
  PIDLIST_ABSOLUTE pidl = ILCreateFromPath(file);
  if (pidl == NULL) {
    pidl = ILCreateFromPath(folder);
  }

  HRESULT hr = E_FAIL;
  if (pidl != NULL) {
    // With no child items, the item that pidl names is selected in its
    // parent folder.
    hr = SHOpenFolderAndSelectItems(pidl, 0, NULL, 0);
    ILFree(pidl);
  }

  if (SUCCEEDED(hrInit)) {
    CoUninitialize();
  }

  if (pidl == NULL) {
    throw SystemException(_T("Cannot find the folder to show"));
  }
  if (FAILED(hr)) {
    throw SystemException(hr);
  }
}
