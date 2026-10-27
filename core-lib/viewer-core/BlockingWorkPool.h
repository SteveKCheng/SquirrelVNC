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

#ifndef _BLOCKING_WORK_POOL_H_
#define _BLOCKING_WORK_POOL_H_

#include <asio/any_io_executor.hpp>
#include <asio/thread_pool.hpp>

#include <mutex>
#include <optional>

//
// A thread for work that blocks, such as deleting local files, which must
// not run on the network thread (NetworkThread). The thread is started only
// on first use (getExecutor()), so that a session that never needs it
// does not have it, and stays until this object is destroyed.
//
// The viewer makes one, as it makes its NetworkThread (TvnViewer), and
// destroys it at exit, after the connections, whose blocking work keeps
// their owners alive until it ends (FileTransferCapability).
//
// Threads: getExecutor() may be called from any thread. Not copyable or
// movable.
//
class BlockingWorkPool final
{
public:
  //
  // Post-conditions: no thread is running.
  //
  BlockingWorkPool();

  BlockingWorkPool(const BlockingWorkPool &) = delete;
  BlockingWorkPool &operator=(const BlockingWorkPool &) = delete;
  BlockingWorkPool(BlockingWorkPool &&) = delete;
  BlockingWorkPool &operator=(BlockingWorkPool &&) = delete;

  //
  // Waits until no work is left, then until the thread, if started, has
  // ended.
  //
  // Pre-conditions: not called on the pool's thread; the work posted
  // ends by itself.
  //
  ~BlockingWorkPool();

  //
  // The executor of the pool's thread, which is started on the first call.
  //
  // Post-conditions: the thread is running.
  // Throws std::system_error if it cannot be started.
  //
  asio::any_io_executor getExecutor();

private:
  std::mutex m_lock;
  // Made by the first getExecutor(); guarded by m_lock.
  std::optional<asio::thread_pool> m_pool;
};

#endif
