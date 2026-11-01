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
//

//
// Unit tests of StringStorage.
//

#include "UnitTest.h"

#include "util/StringStorage.h"

void runStringStorageTests()
{
  StringStorage name(_T("JpegImageQuality"));
  name.toAsciiLowerCase();
  check(name.isEqualTo(_T("jpegimagequality")), "toAsciiLowerCase() lowers ASCII letters");

  name.setString(_T("view-only_2=YES"));
  name.toAsciiLowerCase();
  check(name.isEqualTo(_T("view-only_2=yes")), "toAsciiLowerCase() leaves ASCII non-letters alone");

  // Upper-case A with diaeresis, capital I with dot above, Greek capital
  // sigma: none is ASCII, so none changes, whatever the locale.
  name.setString(_T("\u00C4\u0130\u03A3I"));
  name.toAsciiLowerCase();
  check(name.isEqualTo(_T("\u00C4\u0130\u03A3i")), "toAsciiLowerCase() leaves non-ASCII letters alone");

  name.setString(_T(""));
  name.toAsciiLowerCase();
  check(name.isEmpty(), "toAsciiLowerCase() keeps an empty string empty");
}
