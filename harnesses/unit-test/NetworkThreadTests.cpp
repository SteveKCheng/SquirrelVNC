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
// Tests of NetworkThread (viewer-core/NetworkThread.h), which also check
// that Asio and its C++20 coroutines work in this build.
//

#include "UnitTest.h"

#include "viewer-core/NetworkThread.h"

#include <asio/co_spawn.hpp>
#include <asio/detached.hpp>
#include <asio/post.hpp>
#include <asio/steady_timer.hpp>
#include <asio/use_awaitable.hpp>

#include <atomic>
#include <chrono>
#include <thread>

namespace
{
  asio::awaitable<void> waitThenSet(std::atomic<bool> &done,
                                    std::thread::id &runsOn)
  {
    asio::steady_timer timer(co_await asio::this_coro::executor);
    timer.expires_after(std::chrono::milliseconds(10));
    co_await timer.async_wait(asio::use_awaitable);
    runsOn = std::this_thread::get_id();
    done = true;
  }
}

void runNetworkThreadTests()
{
  std::atomic<bool> posted = false;
  std::atomic<bool> awaited = false;
  std::thread::id postedOn;
  std::thread::id awaitedOn;
  {
    NetworkThread network;
    asio::post(network.getExecutor(), [&] {
      postedOn = std::this_thread::get_id();
      posted = true;
    });
    asio::co_spawn(network.getExecutor(), waitThenSet(awaited, awaitedOn),
                   asio::detached);
    network.join();
  }
  check(posted, "NetworkThread runs work posted to it");
  check(awaited, "NetworkThread runs an Asio coroutine that awaits a timer, "
                 "and join() waits for it");
  check(postedOn == awaitedOn && postedOn != std::this_thread::get_id(),
        "NetworkThread runs everything on one thread of its own");
}
