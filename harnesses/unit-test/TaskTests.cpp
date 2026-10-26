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
// Tests of Task, syncWait() (util/Task.h) and AwaitableReader
// (io-lib/AwaitableReader.h).
//

#include "UnitTest.h"

#include "io-lib/AwaitableReader.h"
#include "io-lib/ByteArrayInputStream.h"
#include "io-lib/IOException.h"
#include "util/Task.h"

#include <memory>
#include <stdexcept>

namespace
{
  // Counts the objects alive, to check what coroutine frames destroy.
  int g_alive = 0;

  struct Tracked
  {
    Tracked() { g_alive++; }
    Tracked(const Tracked &) { g_alive++; }
    ~Tracked() { g_alive--; }
  };

  // Counts the coroutine bodies that have started.
  int g_bodies = 0;

  Task<int> answer()
  {
    g_bodies++;
    co_return 42;
  }

  Task<> nothing()
  {
    g_bodies++;
    co_return;
  }

  Task<int> addOne()
  {
    int value = co_await answer();
    co_return value + 1;
  }

  Task<int> fail()
  {
    g_bodies++;
    if (g_bodies > 0) {
      throw std::runtime_error("fail");
    }
    co_return 0;
  }

  Task<int> catchFailure()
  {
    try {
      co_await fail();
    } catch (const std::runtime_error &) {
      co_return -1;
    }
    co_return 0;
  }

  Task<int> holdLocal()
  {
    Tracked local;
    co_return g_alive;
  }

  Task<> takeArgument(Tracked)
  {
    g_bodies++;
    co_return;
  }

  Task<std::unique_ptr<int>> makeUnique()
  {
    co_return std::make_unique<int>(7);
  }

  Task<> finishAtOnce()
  {
    co_return;
  }

  // count awaits of Tasks that finish without suspending.
  Task<int> awaitMany(int count)
  {
    int done = 0;
    for (int i = 0; i < count; i++) {
      co_await finishAtOnce();
      done++;
    }
    co_return done;
  }

  // A chain of depth Tasks, each awaiting the next.
  Task<int> awaitNested(int depth)
  {
    if (depth == 0) {
      co_return 0;
    }
    co_return 1 + co_await awaitNested(depth - 1);
  }

  Task<> suspendForever()
  {
    co_await std::suspend_always();
  }

  struct Header
  {
    UINT8 type;
    UINT16 width;
    UINT32 length;
    char name[2];
  };

  Task<Header> readHeader(AwaitableReader &reader)
  {
    Header header;
    header.type = co_await reader.readUInt8();
    header.width = co_await reader.readUInt16();
    header.length = co_await reader.readUInt32();
    co_await reader.readFully(header.name, sizeof(header.name));
    co_return header;
  }

  Task<UINT32> readPastEnd(AwaitableReader &reader)
  {
    co_return co_await reader.readUInt32();
  }
}

void runTaskTests()
{
  g_bodies = 0;
  {
    Task<int> task = answer();
    check(g_bodies == 0, "Task does not start its body when called");
  }
  check(g_bodies == 0, "Task destroyed without being awaited never runs its body");

  check(syncWait(answer()) == 42, "syncWait returns the Task's value");
  check(syncWait(addOne()) == 43, "co_await returns the awaited Task's value");

  g_bodies = 0;
  syncWait(nothing());
  check(g_bodies == 1, "syncWait runs a Task<void> once");

  {
    Task<int> stored = answer();
    Task<int> moved(std::move(stored));
    check(syncWait(std::move(moved)) == 42, "a moved Task runs as the original would");
  }

  bool threw = false;
  try {
    syncWait(fail());
  } catch (const std::runtime_error &) {
    threw = true;
  }
  check(threw, "syncWait rethrows the exception that left the body");
  check(syncWait(catchFailure()) == -1,
        "co_await rethrows the exception in the awaiting coroutine");

  g_alive = 0;
  check(syncWait(holdLocal()) == 1, "a coroutine's local lives while its body runs");
  check(g_alive == 0, "a finished coroutine's locals are destroyed");

  g_alive = 0;
  g_bodies = 0;
  {
    Tracked argument;
    Task<> task = takeArgument(argument);
    check(g_alive == 2, "a Task's frame holds a copy of an argument passed by value");
  }
  check(g_alive == 0 && g_bodies == 0,
        "a Task destroyed without being awaited destroys its arguments");

  std::unique_ptr<int> unique = syncWait(makeUnique());
  check(unique && *unique == 7, "Task returns a move-only value");

  // Without symmetric transfer as a tail call, each await would nest the
  // stack deeper, and this would overflow it.
  check(syncWait(awaitMany(1000000)) == 1000000,
        "1,000,000 awaits of Tasks that finish at once run in bounded stack");
  check(syncWait(awaitNested(100000)) == 100000,
        "a chain of 100,000 nested Tasks runs in bounded stack");

  threw = false;
  try {
    syncWait(suspendForever());
  } catch (const std::logic_error &) {
    threw = true;
  }
  check(threw, "syncWait throws std::logic_error for a Task that suspends");

  const char bytes[] = { 0x05, 0x01, 0x02, 0x00, 0x00, 0x01, 0x00, 'h', 'i' };
  ByteArrayInputStream byteStream(bytes, sizeof(bytes));
  DataInputStream dataStream(&byteStream);
  AwaitableReader reader(dataStream);
  Header header = syncWait(readHeader(reader));
  check(header.type == 5 && header.width == 0x0102 && header.length == 0x100 &&
        header.name[0] == 'h' && header.name[1] == 'i',
        "AwaitableReader reads as DataInputStream does, big-endian");

  threw = false;
  try {
    syncWait(readPastEnd(reader));
  } catch (const IOException &) {
    threw = true;
  }
  check(threw, "AwaitableReader throws the stream's IOException from the co_await");
}
