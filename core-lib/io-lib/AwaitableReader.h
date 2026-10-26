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

#ifndef _AWAITABLE_READER_H_
#define _AWAITABLE_READER_H_

#include "DataInputStream.h"

#include <coroutine>
#include <utility>

//
// Reads typed data for coroutines (util/Task.h): each read returns an
// awaitable, to be awaited at once, in the same full expression:
//
//   UINT16 width = co_await reader.readUInt16();
//
// The reads, their byte order and their exceptions are those of
// DataInputStream, which this class wraps.
//
// For now (notes/async-networking.md, Phase 1 step 4) every awaitable is
// ready at once, and does its read, blocking, when the co_await resumes:
// a coroutine that reads through this class runs exactly like an ordinary
// function, so that syncWait() can run it. Phase 3 replaces the inside of
// this class with a buffer that suspends the coroutine when it runs short;
// its callers stay as they are.
//
// Threads: as DataInputStream; one thread reads.
//
// Objects of this class cannot be copied or moved.
//
class AwaitableReader final
{
public:
  //
  // Awaits one read: see the class comment. Read is a function object that
  // does the read and returns its result.
  //
  // [[nodiscard]], so that a read that is not awaited, and so never
  // happens, is a warning.
  //
  template<class Read>
  class [[nodiscard]] Awaitable final
  {
  public:
    explicit Awaitable(Read read)
    : m_read(std::move(read))
    {
    }

    // Always ready: the coroutine never suspends here, for now.
    bool await_ready() const noexcept
    {
      return true;
    }

    // Never called, as await_ready() is true.
    void await_suspend(std::coroutine_handle<>) const noexcept
    {
    }

    // Does the read: returns its result, or throws what it throws.
    decltype(auto) await_resume()
    {
      return m_read();
    }

  private:
    Read m_read;
  };

  //
  // Pre-conditions: input outlives this object, and is read only through
  // it while it is in use.
  //
  explicit AwaitableReader(DataInputStream &input) noexcept
  : m_input(input)
  {
  }

  AwaitableReader(const AwaitableReader &) = delete;
  AwaitableReader &operator=(const AwaitableReader &) = delete;
  AwaitableReader(AwaitableReader &&) = delete;
  AwaitableReader &operator=(AwaitableReader &&) = delete;

  auto readUInt8() { return makeAwaitable([this] { return m_input.readUInt8(); }); }
  auto readUInt16() { return makeAwaitable([this] { return m_input.readUInt16(); }); }
  auto readUInt32() { return makeAwaitable([this] { return m_input.readUInt32(); }); }
  auto readUInt64() { return makeAwaitable([this] { return m_input.readUInt64(); }); }

  auto readInt8() { return makeAwaitable([this] { return m_input.readInt8(); }); }
  auto readInt16() { return makeAwaitable([this] { return m_input.readInt16(); }); }
  auto readInt32() { return makeAwaitable([this] { return m_input.readInt32(); }); }
  auto readInt64() { return makeAwaitable([this] { return m_input.readInt64(); }); }

  //
  // As DataInputStream::readFully(). buffer must stay valid until the
  // co_await completes.
  //
  auto readFully(void *buffer, size_t len)
  {
    return makeAwaitable([this, buffer, len] { m_input.readFully(buffer, len); });
  }

  //
  // As DataInputStream::readUTF8(). storage must stay valid until the
  // co_await completes.
  //
  auto readUTF8(StringStorage *storage)
  {
    return makeAwaitable([this, storage] { m_input.readUTF8(storage); });
  }

private:
  template<class Read>
  static Awaitable<Read> makeAwaitable(Read read)
  {
    return Awaitable<Read>(std::move(read));
  }

  DataInputStream &m_input;
};

#endif // _AWAITABLE_READER_H_
