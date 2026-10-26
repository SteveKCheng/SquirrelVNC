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

#ifndef _TASK_H_
#define _TASK_H_

#include <cassert>
#include <coroutine>
#include <exception>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

//
// The result of a C++20 coroutine that computes a T, or fails with an
// exception, and is awaited by exactly one other coroutine (or by
// syncWait()). Written for the read path of the protocol code; the plan is
// in notes/async-networking.md, "Plan for the rewrite".
//
// Typical use:
//
//   Task<UINT32> readCount(AwaitableReader &reader)
//   {
//     UINT32 count = co_await reader.readUInt32();
//     co_return count;
//   }
//
//   Task<> readList(AwaitableReader &reader)
//   {
//     UINT32 count = co_await readCount(reader);
//     ...
//   }
//
// Semantics:
//
// - Lazy: calling the coroutine allocates its frame and copies its
//   arguments into it, but runs none of its body. The body starts when
//   the Task is awaited, or passed to syncWait().
// - Await at once, and only once: operator co_await takes only an rvalue,
//   as in "co_await f(...)". A Task stored in a variable needs
//   "co_await std::move(task)", which is meant to stand out in review:
//   until it is awaited, the arguments the coroutine refers to (references,
//   pointers, lambda captures) must stay alive, and C++ does not check
//   that. Awaited in the same full expression as the call, temporaries
//   passed to it live long enough.
// - Symmetric transfer: awaiting a Task resumes its coroutine, and its
//   completion resumes the awaiting one, each as a tail call rather than a
//   nested call. A loop of awaits that complete without suspending
//   therefore runs in constant stack space (checked by TaskTests.cpp).
// - Exceptions: one leaving the coroutine's body is stored, and rethrown
//   by the co_await (or syncWait()) that awaits the Task.
// - Threads: none of this is thread-safe. A Task is awaited on the thread
//   that resumes the awaiting coroutine; nothing here moves work to
//   another thread.
// - Frames are allocated with the global operator new (or elided by the
//   compiler when it can prove the lifetimes nest).
//
// T is the result type: void (the default), or an object type that can be
// move-constructed. References are not supported.
//
// Objects of this class can be moved from (leaving an empty Task, which
// must not be awaited), but not copied or assigned.
//
template<class T = void>
class Task;

namespace TaskDetail
{
  //
  // The parts of a Task's promise that do not depend on T.
  //
  class PromiseBase
  {
  public:
    // Lazy: the body does not run until the Task is awaited.
    std::suspend_always initial_suspend() const noexcept
    {
      return {};
    }

    //
    // Suspends a finished coroutine, and resumes the one awaiting it by
    // symmetric transfer; for syncWait(), which has no awaiting coroutine,
    // m_continuation is std::noop_coroutine(), so that control returns to
    // the resume() there.
    //
    class FinalAwaiter final
    {
    public:
      bool await_ready() const noexcept
      {
        return false;
      }

      template<class Promise>
      std::coroutine_handle<> await_suspend(std::coroutine_handle<Promise> finished) const noexcept
      {
        return finished.promise().m_continuation;
      }

      void await_resume() const noexcept
      {
      }
    };

    FinalAwaiter final_suspend() const noexcept
    {
      return {};
    }

    void unhandled_exception() noexcept
    {
      m_exception = std::current_exception();
    }

    // The coroutine to resume when this one finishes.
    std::coroutine_handle<> m_continuation = std::noop_coroutine();
    // The exception that left the body, if any.
    std::exception_ptr m_exception;
    // Set when the body is first resumed: by Task's awaiter, or syncWait().
    bool m_isStarted = false;

  protected:
    void rethrowIfFailed() const
    {
      if (m_exception) {
        std::rethrow_exception(m_exception);
      }
    }
  };

  template<class T>
  class Promise final : public PromiseBase
  {
  public:
    Task<T> get_return_object() noexcept;

    // U defaults to T so that "co_return {...};" works too.
    template<class U = T>
      requires std::is_constructible_v<T, U &&>
    void return_value(U &&value) noexcept(std::is_nothrow_constructible_v<T, U &&>)
    {
      m_value.emplace(std::forward<U>(value));
    }

    //
    // Pre-conditions: the coroutine has finished.
    // Returns the value from co_return, moved out, or rethrows the
    // exception that left the body.
    //
    T takeResult()
    {
      rethrowIfFailed();
      return std::move(*m_value);
    }

  private:
    std::optional<T> m_value;
  };

  template<>
  class Promise<void> final : public PromiseBase
  {
  public:
    Task<void> get_return_object() noexcept;

    void return_void() const noexcept
    {
    }

    //
    // Pre-conditions: the coroutine has finished.
    // Rethrows the exception that left the body, if any.
    //
    void takeResult() const
    {
      rethrowIfFailed();
    }
  };
}

template<class T>
class [[nodiscard]] Task final
{
  static_assert(!std::is_reference_v<T>, "Task<T> does not support references");
  static_assert(std::is_void_v<T> || std::is_move_constructible_v<T>,
                "Task<T> needs a T that can be move-constructed");

public:
  using promise_type = TaskDetail::Promise<T>;

  //
  // Post-conditions: this Task has other's coroutine; other is empty.
  //
  Task(Task &&other) noexcept
  : m_handle(std::exchange(other.m_handle, {}))
  {
  }

  Task(const Task &) = delete;
  Task &operator=(const Task &) = delete;
  Task &operator=(Task &&) = delete;

  //
  // Destroys the coroutine's frame, if this Task has one: the arguments it
  // holds, and the result or exception it keeps.
  //
  // Pre-conditions (asserted): the coroutine has not started, or has
  // finished. A started coroutine that never finishes is a coroutine left
  // suspended, which the design does not allow: an aborted connection
  // makes its reads throw, so that coroutines unwind by themselves
  // (notes/async-networking.md, "Corrections and decisions").
  //
  ~Task()
  {
    if (m_handle) {
      assert(!m_handle.promise().m_isStarted || m_handle.done());
      m_handle.destroy();
    }
  }

  //
  // Awaits the Task: see operator co_await.
  //
  class Awaiter final
  {
  public:
    explicit Awaiter(std::coroutine_handle<promise_type> handle) noexcept
    : m_handle(handle)
    {
    }

    // A Task has not started when awaited, so this is always false.
    bool await_ready() const noexcept
    {
      return false;
    }

    // Starts the coroutine by symmetric transfer, to resume awaiting when
    // it finishes.
    std::coroutine_handle<> await_suspend(std::coroutine_handle<> awaiting) const noexcept
    {
      promise_type &promise = m_handle.promise();
      promise.m_continuation = awaiting;
      promise.m_isStarted = true;
      return m_handle;
    }

    // Returns the result, or rethrows the exception that left the body.
    T await_resume() const
    {
      return m_handle.promise().takeResult();
    }

  private:
    std::coroutine_handle<promise_type> m_handle;
  };

  //
  // Runs the coroutine until it finishes, and gives its result to the
  // awaiting coroutine. The coroutine may suspend in between, as often as
  // its own awaits do; the awaiting coroutine is suspended until then.
  //
  // Pre-conditions (asserted): this Task is not empty, and has not been
  // awaited or passed to syncWait() before.
  // Post-conditions: when the co_await completes, the coroutine has
  // finished; its frame is destroyed with this Task, normally at the end
  // of the full expression.
  // Throws, from the co_await, the exception that left the coroutine's
  // body.
  //
  Awaiter operator co_await() && noexcept
  {
    assert(m_handle && !m_handle.promise().m_isStarted);
    return Awaiter(m_handle);
  }

  //
  // The interface for a driver: code that runs the root of a chain of
  // Tasks from ordinary code, and resumes it when it suspends (see
  // notes/async-networking-resumption.md). syncWait() and runBlocking()
  // (io-lib/AwaitableReader.h) are drivers. Coroutines await Tasks
  // instead.
  //

  //
  // Runs the coroutine until it first suspends, or finishes.
  //
  // Pre-conditions (asserted): this Task is not empty, and has not been
  // started or awaited.
  // Post-conditions: the coroutine has started. If it suspended, whoever
  // it is waiting for must resume it, until isDone(): destroying this Task
  // before then is a bug (see ~Task()).
  //
  void start()
  {
    assert(m_handle && !m_handle.promise().m_isStarted);
    m_handle.promise().m_isStarted = true;
    m_handle.resume();
  }

  //
  // Whether the coroutine has finished, with a value or an exception.
  //
  // Pre-conditions: this Task is not empty.
  //
  bool isDone() const noexcept
  {
    return m_handle.done();
  }

  //
  // Returns the coroutine's result, moved out, or rethrows the exception
  // that left its body.
  //
  // Pre-conditions (asserted): isDone(); and, for a value, result() has
  // not been called before.
  //
  T result()
  {
    assert(m_handle && m_handle.done());
    return m_handle.promise().takeResult();
  }

private:
  friend promise_type;

  template<class U>
  friend U syncWait(Task<U> task);

  explicit Task(std::coroutine_handle<promise_type> handle) noexcept
  : m_handle(handle)
  {
  }

  // Empty only after being moved from.
  std::coroutine_handle<promise_type> m_handle;
};

template<class T>
Task<T> TaskDetail::Promise<T>::get_return_object() noexcept
{
  return Task<T>(std::coroutine_handle<Promise>::from_promise(*this));
}

inline Task<void> TaskDetail::Promise<void>::get_return_object() noexcept
{
  return Task<void>(std::coroutine_handle<Promise>::from_promise(*this));
}

//
// Runs a Task from ordinary code, and returns its result: the bridge
// between code that is already a coroutine and code that is not yet
// (notes/async-networking.md, Phase 2). It does not wait for anything: the
// Task must finish without suspending. A Task that reads through an
// AwaitableReader needs runBlocking() instead, which feeds the reader.
//
// Pre-conditions (asserted): task is not empty, and has not been awaited.
// Post-conditions: the coroutine has finished, and its frame is destroyed.
// Throws the exception that left the coroutine's body; or
// std::logic_error if the coroutine suspended instead of finishing, a bug
// in the caller. In that case the frame is deliberately leaked: destroying
// it would also destroy the Tasks it is awaiting, started but unfinished,
// which ~Task() treats as a bug in its own right.
//
template<class T>
T syncWait(Task<T> task)
{
  task.start();
  if (!task.isDone()) {
    task.m_handle = {};
    throw std::logic_error("syncWait(): the task suspended instead of finishing");
  }
  return task.result();
}

#endif // _TASK_H_
