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

#ifndef _WIN_CLIPBOARD_H_
#define _WIN_CLIPBOARD_H_

#include "util/CommonHeader.h"

class WinClipboard final
{
public:

  WinClipboard(HWND hwnd);
  ~WinClipboard();

  void setHWnd(HWND hwnd);
  
  //
  // Post-conditions: returns true and sets *str to the text on the
  // clipboard, with CR+LF as LF, up to its first null character or the
  // end of the clipboard's memory; returns false, leaving *str unchanged,
  // if there is no text or the clipboard cannot be opened.
  //
  bool getString(StringStorage *str);

  //
  // Puts the text of str on the clipboard, with LF as CR+LF, in place of
  // all its contents.
  //
  // Post-conditions: returns true if the clipboard holds the text; false,
  // leaving the clipboard unchanged, if it could not be put there. The
  // memory given to the clipboard then belongs to the system.
  //
  bool setString(const StringStorage *str);

protected:
  static const TCHAR CR = _T('\r');
  static const TCHAR LF = _T('\n');

protected:
  // function removed CR before LF
  StringStorage removeCR(const StringStorage &str);

  // function replaced LF to CR+LF. If before LF already is CR, this not added second
  StringStorage addCR(const StringStorage *str);

  HWND m_hWnd;
};

#endif
