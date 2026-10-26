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

#include "InputStream.h"
#include "IOException.h"

#include "util/inttypes.h"
#include "util/StringStorage.h"
#include "util/Task.h"

#include <coroutine>
#include <cstddef>
#include <exception>
#include <functional>
#include <future>
#include <stdexcept>
#include <type_traits>
#include <vector>

//
// The bytes of a connection, read by the protocol code, which is made of
// coroutines (util/Task.h), and fed by a driver, which owns the connection.
// Each read returns an awaitable, to be awaited at once, in the same full
// expression:
//
//   UINT16 width = co_await reader.readUInt16();
//
// Despite its name, it does no I/O itself. It keeps a little of the
// connection's state:
//
// - the bytes received and not yet read;
// - the read that is waiting for more of them, if any, and the one
//   coroutine that awaits it;
// - a request that the protocol code has made to the driver, if any, for
//   something other than bytes (the VNC password);
// - an error, once the connection has failed or is being stopped: every
//   read from then on throws it.
//
// A read of bytes that have arrived completes at once, without
// suspending. Otherwise the awaiting coroutine suspends, and control
// returns to the driver, which reads more from the connection, gives the
// bytes to append(), and calls resumeWaiter(). How this works, and why
// the driver alone resumes, is explained in
// notes/async-networking-resumption.md.
//
// The reads, their byte order (big-endian) and their limits are those of
// DataInputStream.
//
// Threads: none of this is thread-safe. The protocol code and the driver
// run on one thread, the driver resuming the protocol code.
//
// Objects of this class cannot be copied or moved: the suspended reads
// and the driver refer to it.
//
class AwaitableReader final
{
public:
  //
  // A request from the protocol code to the driver, for something other
  // than bytes: see request(). The protocol code makes one, and keeps it
  // until the co_await on request() completes; the driver serves it, by
  // starting it and waiting, without blocking, until it is done.
  //
  class Request
  {
  public:
    virtual ~Request() = default;

    //
    // Starts doing what was asked, without waiting for it. Called by the
    // driver (after takeRequest()), outside the coroutines.
    //
    // Post-conditions: done is called exactly once, on any thread, when
    // the request has been served (with null) or has failed (with the
    // exception, which the driver gives to fail(), so that the co_await on
    // request() throws it); possibly before start() returns.
    //
    virtual void start(std::function<void(std::exception_ptr)> done) = 0;

    //
    // Asks for the request to end early, as when the connection stops:
    // done is still called, with an exception, if it has not been. On the
    // driver's thread.
    //
    virtual void cancel() noexcept = 0;
  };

  //
  // Awaits a read of len bytes into dst; see the class comment. Made by the
  // functions below. Not copyable or movable: the reader keeps a pointer to
  // it while its coroutine is suspended.
  //
  class [[nodiscard]] BytesAwaiter
  {
  public:
    BytesAwaiter(AwaitableReader &reader, void *dst, size_t len) noexcept
    : m_reader(reader),
      m_dst(static_cast<std::byte *>(dst)),
      m_len(len)
    {
    }

    BytesAwaiter(const BytesAwaiter &) = delete;
    BytesAwaiter &operator=(const BytesAwaiter &) = delete;

    // True, after copying them, if the bytes have arrived; or if the
    // connection has failed, for await_resume() to throw.
    bool await_ready()
    {
      return m_reader.tryRead(m_dst, m_len);
    }

    // Records the read, with what has arrived of it so far, and waits.
    void await_suspend(std::coroutine_handle<> waiting)
    {
      m_reader.waitForBytes(waiting, m_dst, m_len);
    }

    // Throws the connection's error, if it has failed.
    void await_resume() const
    {
      m_reader.throwIfFailed();
    }

  private:
    AwaitableReader &m_reader;
    std::byte *m_dst;
    size_t m_len;
  };

  //
  // Awaits a read of a big-endian integer of type T; see BytesAwaiter.
  //
  template<class T>
  class [[nodiscard]] ValueAwaiter final : public BytesAwaiter
  {
    static_assert(std::is_integral_v<T>);

  public:
    explicit ValueAwaiter(AwaitableReader &reader) noexcept
    : BytesAwaiter(reader, m_bytes, sizeof(T))
    {
    }

    T await_resume() const
    {
      BytesAwaiter::await_resume();
      std::make_unsigned_t<T> value = 0;
      for (size_t i = 0; i < sizeof(T); i++) {
        value = static_cast<std::make_unsigned_t<T>>(
          (value << 8) | static_cast<UINT8>(m_bytes[i]));
      }
      return static_cast<T>(value);
    }

  private:
    std::byte m_bytes[sizeof(T)] = {};
  };

  //
  // Awaits a request: see request(). Not copyable or movable.
  //
  class [[nodiscard]] RequestAwaiter final
  {
  public:
    RequestAwaiter(AwaitableReader &reader, Request &request) noexcept
    : m_reader(reader),
      m_request(request)
    {
    }

    RequestAwaiter(const RequestAwaiter &) = delete;
    RequestAwaiter &operator=(const RequestAwaiter &) = delete;

    // Ready only if the connection has failed, for await_resume() to throw.
    bool await_ready() const noexcept
    {
      return m_reader.isFailed();
    }

    void await_suspend(std::coroutine_handle<> waiting)
    {
      m_reader.waitForRequest(waiting, m_request);
    }

    // Throws the connection's error, or what serving the request threw.
    void await_resume() const
    {
      m_reader.throwIfFailed();
    }

  private:
    AwaitableReader &m_reader;
    Request &m_request;
  };

  //
  // Post-conditions: no bytes, no waiter, no request, no error.
  //
  AwaitableReader();

  AwaitableReader(const AwaitableReader &) = delete;
  AwaitableReader &operator=(const AwaitableReader &) = delete;
  AwaitableReader(AwaitableReader &&) = delete;
  AwaitableReader &operator=(AwaitableReader &&) = delete;

  //
  // The reads, for the protocol code. Each throws, from the co_await, the
  // connection's error if it has failed: whatever the driver passed to
  // fail(), such as the IOException of a closed connection.
  //

  ValueAwaiter<UINT8> readUInt8() { return ValueAwaiter<UINT8>(*this); }
  ValueAwaiter<UINT16> readUInt16() { return ValueAwaiter<UINT16>(*this); }
  ValueAwaiter<UINT32> readUInt32() { return ValueAwaiter<UINT32>(*this); }
  ValueAwaiter<UINT64> readUInt64() { return ValueAwaiter<UINT64>(*this); }

  ValueAwaiter<INT8> readInt8() { return ValueAwaiter<INT8>(*this); }
  ValueAwaiter<INT16> readInt16() { return ValueAwaiter<INT16>(*this); }
  ValueAwaiter<INT32> readInt32() { return ValueAwaiter<INT32>(*this); }
  ValueAwaiter<INT64> readInt64() { return ValueAwaiter<INT64>(*this); }

  //
  // Reads exactly len bytes into buffer, which must stay valid until the
  // co_await completes.
  //
  BytesAwaiter readFully(void *buffer, size_t len)
  {
    return BytesAwaiter(*this, buffer, len);
  }

  //
  // As DataInputStream::readUTF8(): a 32-bit size in bytes, then that many
  // bytes of UTF-8, converted into storage.
  //
  // A coroutine. storage must stay valid until it has finished.
  // Throws IOException if the size is over DataInputStream::MAX_UTF8_SIZE,
  // leaving the string's bytes unread; Exception if the conversion fails;
  // or the connection's error. storage is unchanged whenever it throws.
  //
  Task<> readUTF8(StringStorage *storage);

  //
  // Suspends until the driver has served request (Request::start()). The
  // way for the protocol code to wait for something other than bytes: the
  // driver does the waiting (notes/async-networking-resumption.md,
  // "Requests").
  //
  // Throws, from the co_await, the exception the request failed with, or
  // the connection's error.
  //
  RequestAwaiter request(Request &request)
  {
    return RequestAwaiter(*this, request);
  }

  //
  // For the driver.
  //

  //
  // Whether a coroutine is suspended on this reader, waiting for bytes or
  // for a request to be served.
  //
  bool isWaiting() const noexcept
  {
    return static_cast<bool>(m_waiter);
  }

  //
  // Whether the waiter waits for a request, which the driver takes with
  // takeRequest() and serves, rather than for bytes.
  //
  bool hasRequest() const noexcept
  {
    return m_request != nullptr;
  }

  //
  // Adds len bytes, received from the connection, after those not read
  // yet. Does not resume the waiter: see resumeWaiter().
  //
  void append(const void *data, size_t len);

  //
  // Takes the request that the waiter waits for, for the driver to serve
  // (Request::start()). Once it is done, the driver calls fail() if it
  // failed, then resumeWaiter().
  //
  // Pre-conditions (asserted): hasRequest().
  // Post-conditions: !hasRequest(); the waiter still waits.
  //
  Request &takeRequest();

  //
  // Fails the connection: the waiter, when resumed, and every read after
  // it, throws error. A second failure is ignored: the first error stays.
  //
  // Pre-conditions: error is not empty.
  //
  void fail(std::exception_ptr error) noexcept;

  //
  // Resumes the waiter, if what it waits for is there: all the bytes of its
  // read, which are first copied from those appended; the request served;
  // or an error. Otherwise it stays suspended, for the driver to read more.
  //
  // Pre-conditions (asserted): isWaiting(); and, unless the connection has
  // failed, !hasRequest().
  // Post-conditions: if resumed, the waiter has run until its chain of
  // Tasks suspended again or finished.
  //
  void resumeWaiter();

private:
  // For the awaiters.
  bool tryRead(std::byte *dst, size_t len);
  void waitForBytes(std::coroutine_handle<> waiter, std::byte *dst, size_t len);
  void waitForRequest(std::coroutine_handle<> waiter, Request &request);
  bool isFailed() const noexcept;
  void throwIfFailed() const;

  // Copies up to len of the bytes not read yet to dst; returns how many.
  size_t take(std::byte *dst, size_t len);

  // The bytes received: those from m_start on have not been read yet.
  std::vector<std::byte> m_buffer;
  size_t m_start;

  // The suspended coroutine, if any, and what it waits for: the rest of a
  // read (m_pendingLen bytes still to copy to m_pendingDst), or m_request.
  std::coroutine_handle<> m_waiter;
  std::byte *m_pendingDst;
  size_t m_pendingLen;
  Request *m_request;

  std::exception_ptr m_error;
};

//
// The blocking driver (notes/async-networking-resumption.md, "The
// driver"): runs task, whose coroutines read through reader, to the end,
// reading from input whenever reader runs short, and serving reader's
// requests, waiting on this thread until each is done. Used by RemoteViewerCore on its own thread, by
// rfb-fuzz, and by tests.
//
// Pre-conditions: task has not been started; reader is read by task's
// coroutines only, and has no waiter.
// Post-conditions: task has finished.
// Returns task's result, or throws what left it. An exception from input,
// or from serving a request, does not leave here directly: it fails the
// reader, so the protocol code receives it from its co_await, unwinds, and
// passes it on through task. Throws std::logic_error, leaving task
// suspended, if task waits for anything other than reader: a bug.
//
template<class T>
T runBlocking(Task<T> task, AwaitableReader &reader, InputStream &input)
{
  // InputStream::read() returns what has arrived, up to this many bytes.
  static const size_t CHUNK_SIZE = 64 * 1024;
  std::vector<char> chunk(CHUNK_SIZE);

  task.start();
  while (!task.isDone()) {
    if (!reader.isWaiting()) {
      // A bug: nothing here can resume the task.
      throw std::logic_error("runBlocking(): the task waits for something other than the reader");
    }
    try {
      if (reader.hasRequest()) {
        // Served on this thread, waiting for it to be done.
        std::promise<std::exception_ptr> done;
        reader.takeRequest().start([&done](std::exception_ptr failure) {
          done.set_value(failure);
        });
        std::exception_ptr failure = done.get_future().get();
        if (failure) {
          std::rethrow_exception(failure);
        }
      } else {
        size_t length = input.read(chunk.data(), chunk.size());
        if (length == 0) {
          throw IOException(_T("The stream returned no data"));
        }
        reader.append(chunk.data(), length);
      }
    } catch (...) {
      reader.fail(std::current_exception());
    }
    reader.resumeWaiter();
  }
  return task.result();
}

#endif // _AWAITABLE_READER_H_
