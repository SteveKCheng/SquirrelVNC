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
// Tests of asyncResolve() (network/HostResolver.h), on a NetworkThread.
//

#include "UnitTest.h"

#include "network/HostResolver.h"
#include "viewer-core/NetworkThread.h"

#include <asio/bind_cancellation_slot.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/error.hpp>
#include <asio/post.hpp>

#include <algorithm>
#include <atomic>
#include <future>

namespace
{
  struct Result
  {
    std::error_code error;
    std::vector<asio::ip::tcp::endpoint> endpoints;
  };

  // Resolves host on network's thread, as the drivers will, and waits.
  Result resolve(NetworkThread &network, const TCHAR *host, UINT16 port)
  {
    std::promise<Result> promise;
    StringStorage name(host);
    asio::post(network.getExecutor(), [&] {
      asyncResolve(network.getExecutor(), name, port,
        [&](std::error_code error, std::vector<asio::ip::tcp::endpoint> endpoints) {
          promise.set_value(Result{ error, std::move(endpoints) });
        });
    });
    return promise.get_future().get();
  }

  bool contains(const Result &result, const char *address, UINT16 port)
  {
    asio::ip::tcp::endpoint wanted(asio::ip::make_address_v4(address), port);
    return std::find(result.endpoints.begin(), result.endpoints.end(), wanted) !=
           result.endpoints.end();
  }
}

void runHostResolverTests()
{
  NetworkThread network;

  Result numeric = resolve(network, _T("127.0.0.1"), 5900);
  check(!numeric.error && contains(numeric, "127.0.0.1", 5900),
        "asyncResolve gives a numeric address back, with the port");

  Result local = resolve(network, _T("localhost"), 5901);
  check(!local.error && contains(local, "127.0.0.1", 5901),
        "asyncResolve finds localhost");

  Result unknown = resolve(network, _T("no-such-host.invalid"), 5900);
  check(unknown.error && unknown.endpoints.empty(),
        "asyncResolve reports an error for a name that does not exist");

  // Cancelled at once: whether the lookup had completed or not, the handler
  // is called exactly once, and the loop does not hang.
  std::atomic<int> calls = 0;
  std::promise<std::error_code> cancelled;
  asio::cancellation_signal signal;
  StringStorage slowName(_T("squirrelvnc-cancel-test.example.com"));
  asio::post(network.getExecutor(), [&] {
    asyncResolve(network.getExecutor(), slowName, 5900,
      asio::bind_cancellation_slot(signal.slot(),
        [&](std::error_code error, std::vector<asio::ip::tcp::endpoint>) {
          if (calls++ == 0) {
            cancelled.set_value(error);
          }
        }));
    signal.emit(asio::cancellation_type::all);
  });
  std::error_code cancelError = cancelled.get_future().get();
  network.join();
  check(calls == 1, "asyncResolve calls a cancelled lookup's handler exactly once");
  _tprintf(_T("     (the cancelled lookup ended with: %hs)\n"),
           cancelError == asio::error::operation_aborted ? "operation_aborted"
                                                          : cancelError.message().c_str());
}
