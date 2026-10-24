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
// Unit tests of ScopeExit, the scope guard.
//

#include "UnitTest.h"

#include "util/ScopeExit.h"

#include <stdexcept>
#include <utility>

namespace
{
  int g_calls = 0;

  void countCall()
  {
    g_calls++;
  }

  // A function object whose copy throws, to check that the clean-up
  // still happens when the guard cannot keep a copy.
  struct ThrowingCopy
  {
    int *calls;

    explicit ThrowingCopy(int *c) : calls(c) {}
    ThrowingCopy(const ThrowingCopy &) { throw std::runtime_error("copy"); }
    void operator()() const { ++*calls; }
  };

  ScopeExit<void (*)()> makeGuard()
  {
    ScopeExit<void (*)()> guard(&countCall);
    return guard;
  }
}

void runScopeExitTests()
{
  int calls = 0;
  {
    ScopeExit guard([&] { calls++; });
    check(calls == 0, "ScopeExit does not call the function before the scope ends");
  }
  check(calls == 1, "ScopeExit calls the function once at the end of the scope");

  calls = 0;
  {
    ScopeExit guard([&] { calls++; });
    guard.release();
  }
  check(calls == 0, "ScopeExit does not call the function once released");

  calls = 0;
  try {
    ScopeExit guard([&] { calls++; });
    throw std::runtime_error("leave");
  } catch (const std::runtime_error &) {
  }
  check(calls == 1, "ScopeExit calls the function when an exception leaves the scope");

  calls = 0;
  {
    ScopeExit first([&] { calls++; });
    {
      ScopeExit second(std::move(first));
    }
    check(calls == 1, "ScopeExit moved to calls the function at the end of its scope");
  }
  check(calls == 1, "ScopeExit moved from does not call the function");

  g_calls = 0;
  {
    ScopeExit<void (&)()> byReference(countCall);
  }
  check(g_calls == 1, "ScopeExit holds a reference to a function");

  g_calls = 0;
  {
    ScopeExit<void (*)()> returned = makeGuard();
    check(g_calls == 0, "ScopeExit returned from a function has not called it yet");
  }
  check(g_calls == 1, "ScopeExit returned from a function calls it once");

  calls = 0;
  ThrowingCopy throwing(&calls);
  bool threw = false;
  try {
    ScopeExit guard(throwing);
  } catch (const std::runtime_error &) {
    threw = true;
  }
  check(threw && calls == 1,
        "ScopeExit that cannot copy the function calls it and passes on the exception");
}
