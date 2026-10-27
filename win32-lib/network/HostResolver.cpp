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

#include "HostResolver.h"

#include "util/winhdr.h"

#include <asio/append.hpp>
#include <asio/associated_cancellation_slot.hpp>
#include <asio/dispatch.hpp>
#include <asio/error.hpp>
#include <asio/execution/outstanding_work.hpp>
#include <asio/post.hpp>
#include <asio/prefer.hpp>

#include <atomic>
#include <new>
#include <utility>

namespace
{
  class ResolveOperation;

  // GetAddrInfoExW's OVERLAPPED, with the way back to its operation.
  struct Overlapped
  {
    OVERLAPPED overlapped;
    ResolveOperation *operation;
  };

  //
  // One lookup. It owns itself from start() until its handler has been
  // called (deliver()), since Windows may call onComplete() at any time
  // until then, even after a cancellation.
  //
  // Threads: start(), cancel() and deliver() run on the handler's executor
  // (the network thread); onComplete(), on a thread of the system's. Only
  // m_isSettled, an atomic, and the lookup's own data are shared. No lock
  // is held across GetAddrInfoExCancel(): it waits for a completion
  // routine that is running, which would wait for the lock.
  //
  class ResolveOperation final
  {
  public:
    ResolveOperation(asio::any_io_executor executor, UINT16 port, ResolveHandler handler)
    : m_work(asio::prefer(std::move(executor), asio::execution::outstanding_work.tracked)),
      m_port(port),
      m_handler(std::move(handler))
    {
      m_overlapped.operation = this;
    }

    ResolveOperation(const ResolveOperation &) = delete;
    ResolveOperation &operator=(const ResolveOperation &) = delete;

    //
    // Starts the lookup; completes it at once if GetAddrInfoExW does not
    // go asynchronous (no callback comes then).
    //
    void start(const StringStorage &host)
    {
      m_slot = asio::get_associated_cancellation_slot(m_handler);
      if (m_slot.is_connected()) {
        m_slot.emplace<Canceller>(this);
      }

      ADDRINFOEXW hints = {};
      hints.ai_family = AF_INET;
      hints.ai_socktype = SOCK_STREAM;
      hints.ai_protocol = IPPROTO_TCP;
      INT error = GetAddrInfoExW(host.getString(), nullptr, NS_ALL, nullptr, &hints,
                                 &m_result, nullptr, &m_overlapped.overlapped,
                                 &onComplete, &m_cancelHandle);
      if (error != WSA_IO_PENDING) {
        complete(static_cast<DWORD>(error));
      }
    }

    //
    // Cancels the lookup, unless it has completed or was cancelled before:
    // calling it again does nothing. Called through the cancellation slot,
    // on the network thread, any number of times.
    //
    // A narrow race remains, which the API does not let us close: the
    // completion routine may start, and end, between the exchange and
    // GetAddrInfoExCancel(), so that the handle is no longer valid; the call
    // then fails with WSA_INVALID_HANDLE, and does nothing. .NET accepts
    // the same race (NameResolutionPal.Windows.cs, in dotnet/runtime).
    //
    void cancel()
    {
      // Relaxed: the flag only decides which of complete() and cancel()
      // came first, and m_cancelHandle is written on this thread, before
      // start() returns.
      if (!m_isSettled.exchange(true, std::memory_order_relaxed)
          && m_cancelHandle != nullptr) {
        GetAddrInfoExCancel(&m_cancelHandle);
      }
    }

  private:
    struct Canceller
    {
      ResolveOperation *operation;

      void operator()(asio::cancellation_type)
      {
        operation->cancel();
      }
    };

    static void CALLBACK onComplete(DWORD error, DWORD /* bytes */,
                                    LPWSAOVERLAPPED overlapped)
    {
      reinterpret_cast<Overlapped *>(overlapped)->operation->complete(error);
    }

    //
    // Takes the results, frees the system's list, and posts deliver() to
    // the network thread. Any thread.
    //
    void complete(DWORD error) noexcept
    {
      m_isSettled.store(true, std::memory_order_relaxed);
      std::error_code result;
      std::vector<asio::ip::tcp::endpoint> endpoints;
      try {
        if (error == NO_ERROR) {
          for (ADDRINFOEXW *info = m_result; info != nullptr; info = info->ai_next) {
            if (info->ai_family == AF_INET && info->ai_addrlen >= sizeof(sockaddr_in)) {
              const sockaddr_in *address = reinterpret_cast<const sockaddr_in *>(info->ai_addr);
              endpoints.emplace_back(asio::ip::address_v4(ntohl(address->sin_addr.s_addr)),
                                     m_port);
            }
          }
          if (endpoints.empty()) {
            result = asio::error::host_not_found;
          }
        } else if (error == WSA_E_CANCELLED) {
          result = asio::error::operation_aborted;
        } else {
          result = std::error_code(static_cast<int>(error), asio::error::get_system_category());
        }
      } catch (const std::bad_alloc &) {
        endpoints.clear();
        result = asio::error::no_memory;
      }
      if (m_result != nullptr) {
        FreeAddrInfoExW(m_result);
        m_result = nullptr;
      }
      asio::post(m_work, [this, result, endpoints = std::move(endpoints)]() mutable {
        deliver(result, std::move(endpoints));
      });
    }

    //
    // Frees this operation, then calls the handler on its own executor (at
    // once, if that is the network thread's, as for a coroutine there). On
    // the network thread.
    //
    void deliver(std::error_code result, std::vector<asio::ip::tcp::endpoint> endpoints)
    {
      if (m_slot.is_connected()) {
        m_slot.clear();
      }
      ResolveHandler handler = std::move(m_handler);
      delete this;
      asio::dispatch(asio::append(std::move(handler), result, std::move(endpoints)));
    }

    Overlapped m_overlapped = {};
    ADDRINFOEXW *m_result = nullptr;
    HANDLE m_cancelHandle = nullptr;

    // Set by complete(), or by the first cancel(): the lookup has a result
    // coming, or is being cancelled, so cancel() does nothing more.
    std::atomic<bool> m_isSettled = false;

    // Keeps the loop running (NetworkThread::join() waiting) until the
    // handler has been called.
    asio::any_io_executor m_work;
    UINT16 m_port;
    ResolveHandler m_handler;
    asio::cancellation_slot m_slot;
  };
}

void startResolve(asio::any_io_executor executor, const StringStorage &host,
                  UINT16 port, ResolveHandler handler)
{
  ResolveOperation *operation = new ResolveOperation(std::move(executor), port,
                                                     std::move(handler));
  operation->start(host);
}
