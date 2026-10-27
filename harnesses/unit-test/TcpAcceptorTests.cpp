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
// Tests of TcpAcceptor (viewer-core/TcpAcceptor.h), on a NetworkThread,
// on the loopback address, so that the firewall is not involved.
//

#include "UnitTest.h"

#include "network/socket/SocketException.h"
#include "viewer-core/NetworkThread.h"
#include "viewer-core/TcpAcceptor.h"

#include <asio/io_context.hpp>
#include <asio/post.hpp>
#include <asio/read.hpp>
#include <asio/write.hpp>

#include <atomic>
#include <chrono>
#include <future>
#include <memory>
#include <mutex>
#include <optional>

namespace
{
  const std::chrono::seconds TIMEOUT(10);

  // The connections a TcpAcceptor hands over, for the test thread to wait for.
  struct Accepted
  {
    std::mutex lock;
    std::optional<std::promise<asio::ip::tcp::socket>> next;
    std::atomic<int> count = 0;
  };

  // A handler that holds accepted, so that its release can be seen.
  TcpAcceptor::AcceptHandler handOver(std::shared_ptr<Accepted> accepted)
  {
    return [accepted](asio::ip::tcp::socket socket) {
      ++accepted->count;
      std::lock_guard<std::mutex> lock(accepted->lock);
      if (accepted->next) {
        accepted->next->set_value(std::move(socket));
        accepted->next.reset();
      }
    };
  }

  std::future<asio::ip::tcp::socket> expect(Accepted &accepted)
  {
    std::lock_guard<std::mutex> lock(accepted.lock);
    accepted.next.emplace();
    return accepted.next->get_future();
  }

  // Waits until what was posted to network before has run.
  void roundTrip(NetworkThread &network)
  {
    std::promise<void> done;
    asio::post(network.getExecutor(), [&] { done.set_value(); });
    done.get_future().wait();
  }

  asio::ip::tcp::endpoint loopback(unsigned short port)
  {
    return asio::ip::tcp::endpoint(asio::ip::address_v4::loopback(), port);
  }
}

void runTcpAcceptorTests()
{
  NetworkThread network;
  asio::io_context clients;
  auto accepted = std::make_shared<Accepted>();
  std::weak_ptr<Accepted> handlerRef = accepted;

  std::optional<TcpAcceptor> acceptor;
  acceptor.emplace(network.getExecutor(), loopback(0), handOver(accepted));
  unsigned short port = acceptor->getPort();
  check(port != 0, "TcpAcceptor picks a port for port 0");

  bool inUse = false;
  try {
    TcpAcceptor second(network.getExecutor(), loopback(port), handOver(accepted));
  } catch (const SocketException &) {
    inUse = true;
  }
  check(inUse, "TcpAcceptor throws SocketException for a port in use");

  // A connection is handed over, as the socket of that connection.
  std::future<asio::ip::tcp::socket> next = expect(*accepted);
  asio::ip::tcp::socket client(clients);
  std::error_code error;
  client.connect(loopback(port), error);
  bool handedOver = !error && next.wait_for(TIMEOUT) == std::future_status::ready;
  check(handedOver, "TcpAcceptor hands an accepted connection over");
  if (handedOver) {
    asio::ip::tcp::socket server = next.get();
    char sent = 'x';
    char received = 0;
    asio::write(client, asio::buffer(&sent, 1), error);
    if (!error) {
      asio::read(server, asio::buffer(&received, 1), error);
    }
    check(!error && received == sent &&
          server.remote_endpoint(error) == client.local_endpoint(error),
          "TcpAcceptor's socket is the client's connection");
  }

  // Once the closing has run, the port is free, and nothing more is
  // handed over.
  acceptor->stop();
  roundTrip(network);
  int countBefore = accepted->count;
  bool rebound = false;
  try {
    auto other = std::make_shared<Accepted>();
    TcpAcceptor again(network.getExecutor(), loopback(port), handOver(other));
    std::future<asio::ip::tcp::socket> otherNext = expect(*other);
    asio::ip::tcp::socket client2(clients);
    client2.connect(loopback(port), error);
    rebound = !error && otherNext.wait_for(TIMEOUT) == std::future_status::ready;
  } catch (const SocketException &) {
  }
  check(rebound, "TcpAcceptor frees its port once stop() has taken effect");
  check(accepted->count == countBefore,
        "TcpAcceptor hands nothing over after stop() has taken effect");

  // Stopped twice, then destroyed; and one destroyed without stop(): the
  // loops end, so the join returns, and the handlers are released.
  acceptor->stop();
  acceptor.reset();
  accepted.reset();
  {
    auto unstopped = std::make_shared<Accepted>();
    std::weak_ptr<Accepted> unstoppedRef = unstopped;
    {
      TcpAcceptor destroyed(network.getExecutor(), loopback(0), handOver(unstopped));
    }
    unstopped.reset();
    network.join();
    check(handlerRef.expired() && unstoppedRef.expired(),
          "TcpAcceptor's accept loop ends and releases its handler");
  }
}
