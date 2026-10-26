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

#ifndef _NETWORK_THREAD_H_
#define _NETWORK_THREAD_H_

#include <asio/any_io_executor.hpp>
#include <asio/thread_pool.hpp>

//
// The one thread on which the protocol of every connection runs: an Asio
// event loop (notes/async-networking.md, Phase 4). Each connection's
// driver, an asio::awaitable, is spawned on getExecutor(); the protocol
// code itself only ever runs when its driver resumes it, so on this
// thread (notes/async-networking-resumption.md).
//
// The viewer makes one, at start, and destroys it at exit, after every
// connection has been told to stop: the destructor waits for them to
// finish. That join is the only synchronous wait for the network
// (notes/async-networking.md, "Corrections and decisions"). The program
// owns it (TvnViewer), not a static, so that it is joined before WinSock is
// shut down rather than after main(), and gives its executor to each
// RemoteViewerCore. Test programs make their own, as many as they need.
//
// Threads: getExecutor() may be called from any thread; work is posted to
// the executor from any thread, which is how other threads reach the
// connections. Not copyable or movable.
//
class NetworkThread final
{
public:
  //
  // Post-conditions: the thread is running, waiting for work.
  // Throws std::system_error if it cannot be started.
  //
  NetworkThread();

  NetworkThread(const NetworkThread &) = delete;
  NetworkThread &operator=(const NetworkThread &) = delete;
  NetworkThread(NetworkThread &&) = delete;
  NetworkThread &operator=(NetworkThread &&) = delete;

  //
  // Waits until no work is left, then until the thread has ended: as
  // join(), if it was not called.
  //
  // Pre-conditions: every connection has been told to stop, so that its
  // work ends; otherwise this waits for it.
  //
  ~NetworkThread();

  //
  // The executor of the loop, for spawning connections and posting to them.
  //
  asio::any_io_executor getExecutor() noexcept;

  //
  // Waits until no work is left on the loop, then until the thread has
  // ended. Work posted after that is not run.
  //
  // Pre-conditions: not called on the network thread itself.
  //
  void join();

private:
  asio::thread_pool m_pool;
};

#endif // _NETWORK_THREAD_H_
