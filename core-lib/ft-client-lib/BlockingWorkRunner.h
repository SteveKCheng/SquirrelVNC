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

#ifndef _BLOCKING_WORK_RUNNER_H_
#define _BLOCKING_WORK_RUNNER_H_

#include <functional>

//
// How a file-transfer operation, which runs on the network thread, runs
// work that blocks (local file system calls) on another thread, and gets
// back to the network thread. Implemented by FileTransferCapability, so
// that the operations do not depend on Asio.
//
// Lifetime: the operation that runs work may refer to itself in it: the
// runner keeps the owner of the file transfer, and so the operation, alive
// until the work and what it posts have run.
//
class BlockingWorkRunner
{
public:
  //
  // Runs work on the thread for blocking work. Called on the network
  // thread.
  //
  // Post-conditions: returns whether work will run; false if the owner of
  // the file transfer is being deleted.
  //
  virtual bool runBlocking(std::function<void()> work) = 0;

  //
  // Runs work on the network thread, after what was posted before it.
  // Called from any thread, notably by the work of runBlocking().
  //
  // Pre-conditions: work does not throw.
  //
  virtual void postToNetwork(std::function<void()> work) = 0;

protected:
  // Not deleted through this interface.
  ~BlockingWorkRunner() = default;
};

#endif
