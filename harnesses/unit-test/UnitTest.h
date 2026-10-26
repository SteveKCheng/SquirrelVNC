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

#ifndef _UNIT_TEST_H_
#define _UNIT_TEST_H_

#include "util/CommonHeader.h"

#include <string>
#include <vector>

//
// Records one check: prints "PASS what" or "FAIL what", and counts it.
//
void check(bool ok, const char *what);

//
// Post-conditions: returns the names of the files in directory.
//
std::vector<std::wstring> listFiles(const std::wstring &directory);

//
// The suites. dir is an empty directory for files, removed afterwards.
//
void runIniTests(const std::wstring &dir);
void runHostPathTests();
void runDecoderStoreTests();
void runFileTests(const std::wstring &dir);
void runFileInfoListTests();
void runScopeExitTests();
void runExceptionTests();
void runTaskTests();
void runSocketTests();
void runBufferedInputStreamTests();

#endif // _UNIT_TEST_H_
