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
#include <vector>

//
// The stream under a connection's DataOutputStream: write() only queues
// the bytes, and flush() wakes the connection's writer, which takes them
// with take() and sends them (notes/async-networking.md, Phase 4 step 2b).
// The messages keep the order in which they were written. The queue is the
// only buffer: each writer ends its message with a flush(), so the writer
// is woken once per message, and never takes half of one, since it runs on
// the same thread.
//
// Writing can stay synchronous because each message is small and complete
// in memory before it is flushed here. Messages queued together are sent
// together, in one system call.
//
// Threads: not synchronized; the network thread only. (It had a lock while
// other threads wrote messages; since Phase 4 steps 2e and 5 they post
// events and file-transfer commands instead, and step 7 removed it.)
//
class QueuedOutputStream final : public OutputStream
{
public:
  QueuedOutputStream();
  ~QueuedOutputStream() override;

  QueuedOutputStream(const QueuedOutputStream &) = delete;
  QueuedOutputStream &operator=(const QueuedOutputStream &) = delete;

  //
  // Starts waking the writer: wake is called by each flush() that finds
  // bytes queued. It must only post work to the network thread, and not
  // throw.
  //
  // Pre-conditions: not open, and not closed.
  //
  void open(std::function<void()> wake);

  //
  // Queues len bytes.
  //
  // Post-conditions: returns len; the bytes are queued, and the writer not
  // woken.
  // Throws IOException once closed: the connection has ended.
  //
  size_t write(const void *buffer, size_t len) override;

  //
  // Post-conditions: the writer has been woken, if open and anything is
  // queued.
  //
  void flush() override;

  //
  // Takes all the bytes queued, oldest first; empty if none.
  //
  std::vector<char> take();

  //
  // Ends the queue: no more wakes, and write() throws from now on.
  //
  // Post-conditions: closed; the wake function is not called again.
  //
  void close();

private:
  std::vector<char> m_queued;
  std::function<void()> m_wake;
  bool m_isClosed;
};

#endif // _QUEUED_OUTPUT_STREAM_H_
