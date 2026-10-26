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

#ifndef _QUEUED_OUTPUT_STREAM_H_
#define _QUEUED_OUTPUT_STREAM_H_

#include "io-lib/OutputStream.h"

#include <functional>
#include <mutex>
#include <vector>

//
// The stream under a connection's RfbOutputGate: write() only queues the
// bytes, and wakes the connection's writer on the network thread, which
// takes them with take() and sends them (notes/async-networking.md, Phase
// 4 step 2b). So a thread that sends a message, such as the GUI thread
// sending a key, never waits for the network; and the messages keep the
// order in which they were flushed, whichever thread flushed them.
//
// Writing can stay synchronous because each message is small and complete
// in memory before it is flushed here. Messages queued together are sent
// together, in one system call.
//
// Threads: every function may be called from any thread.
//
class QueuedOutputStream final : public OutputStream
{
public:
  QueuedOutputStream();
  ~QueuedOutputStream() override;

  QueuedOutputStream(const QueuedOutputStream &) = delete;
  QueuedOutputStream &operator=(const QueuedOutputStream &) = delete;

  //
  // Starts waking the writer: wake is called after each write(), with the
  // queue's lock held, so that it is ordered with close(). It must only
  // post work to the network thread, and not throw.
  //
  // Pre-conditions: not open, and not closed.
  //
  void open(std::function<void()> wake);

  //
  // Queues len bytes.
  //
  // Post-conditions: returns len; the writer has been woken, if open.
  // Throws IOException once closed: the connection has ended.
  //
  size_t write(const void *buffer, size_t len) override;

  //
  // Takes all the bytes queued, oldest first; empty if none.
  //
  std::vector<char> take();

  //
  // Ends the queue: no more wakes, and write() throws from now on.
  //
  // Post-conditions: closed. When this returns, no call to the wake
  // function is in progress or to come.
  //
  void close();

private:
  std::mutex m_lock;
  std::vector<char> m_queued;
  std::function<void()> m_wake;
  bool m_isClosed;
};

#endif // _QUEUED_OUTPUT_STREAM_H_
