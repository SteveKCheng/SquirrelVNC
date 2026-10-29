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
// rfb-fuzz: feeds fuzz inputs, in the form ServerInit followed by server
// messages, to the viewer's decoders. See FuzzSession and notes/testing.md.
//
// In the ReleaseFuzz configuration this is a libFuzzer program, which
// libFuzzer's own main() drives: give it a corpus directory to fuzz, or
// files to run once each. In the other configurations it has a main() of
// its own that runs each file named on the command line once, so that the
// corpus and the regression inputs can be checked in every build.
//

#include "log-writer/LogWriter.h"
#include "rfb/FrameBuffer.h"
#include "util/ScopeExit.h"

#include "FuzzNotifier.h"
#include "FuzzSession.h"

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <mutex>
#include <vector>

#ifdef _MSC_VER
#pragma warning(disable: 6262) // suppress warning on use of a ~64KB buffer on the stack
#endif

//
// The notifier shared by every input (see FuzzNotifier).
//
static FuzzNotifier *getNotifier()
{
  static LogWriter logWriter(nullptr);
  static FrameBuffer frameBuffer;
  static std::mutex fbLock;
  static FuzzNotifier notifier(&frameBuffer, &fbLock, &logWriter);
  return &notifier;
}

extern "C" int LLVMFuzzerTestOneInput(const uint8_t *data, size_t size)
{
  FuzzSession session(getNotifier());
  session.run(data, size);
  return 0;
}

#ifndef SQUIRREL_LIBFUZZER

//
// Run each file named on the command line once.
//
// Post-conditions: returns 0 if every file was read and run, 2 if a file
// could not be read or no file was named. A finding crashes the program,
// or in the ReleaseAsan build ends it with ASan's report.
//
int _tmain(int argc, TCHAR *argv[])
{
  if (argc < 2) {
    _ftprintf(stderr, _T("Usage: rfb-fuzz FILE...\n")
                      _T("Runs each file once, as libFuzzer would.\n"));
    return 2;
  }
  for (int i = 1; i < argc; i++) {
    FILE *file = nullptr;
    if (_tfopen_s(&file, argv[i], _T("rb")) != 0) {
      _ftprintf(stderr, _T("Cannot open %s\n"), argv[i]);
      return 2;
    }
    ScopeExit closeFile([&] { fclose(file); });
    std::vector<uint8_t> data;
    uint8_t buffer[65536];
    size_t count;
    while ((count = fread(buffer, 1, sizeof(buffer), file)) > 0) {
      data.insert(data.end(), buffer, buffer + count);
    }
    bool ok = ferror(file) == 0;
    if (!ok) {
      _ftprintf(stderr, _T("Cannot read %s\n"), argv[i]);
      return 2;
    }
    LLVMFuzzerTestOneInput(data.data(), data.size());
    _ftprintf(stderr, _T("ok %s\n"), argv[i]);
  }
  return 0;
}

#endif // SQUIRREL_LIBFUZZER
