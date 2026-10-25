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

#include <optional>

class WinClipboard final
{
public:

  WinClipboard(HWND hwnd);
  ~WinClipboard();

  //
  // Sets the window that opens the clipboard, and so owns it after
  // setString().
  //
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
  // On true, the clipboard sequence number of this change is recorded,
  // for isUnchangedSinceSetString().
  //
  bool setString(const StringStorage &str);

  //
  // Returns true if the clipboard is unchanged since the last successful
  // setString() of this object, that is, it still holds that text and
  // no one else has changed it since. A clipboard listener uses this to
  // tell the notification of its own change from those of others.
  //
  // Post-conditions: false if setString() has never succeeded, or if
  // the sequence number was unavailable when it last did (the window
  // station gives no clipboard access).
  //
  // Another program's change between the end of setString() and its
  // reading of the number (one call apart) would be taken for ours.
  //
  bool isUnchangedSinceSetString() const;

protected:
  static const TCHAR CR = _T('\r');
  static const TCHAR LF = _T('\n');

protected:
  // function removed CR before LF
  StringStorage removeCR(const StringStorage &str);

  // function replaced LF to CR+LF. If before LF already is CR, this not added second
  StringStorage addCR(const StringStorage &str);

  HWND m_hWnd;

  // GetClipboardSequenceNumber() just after the last successful
  // setString(); empty before it, or when the number was unavailable.
  std::optional<DWORD> m_setStringSequenceNumber;
};

#endif
