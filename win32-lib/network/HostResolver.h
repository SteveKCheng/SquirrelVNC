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

#ifndef _HOST_RESOLVER_H_
#define _HOST_RESOLVER_H_

#include "util/inttypes.h"
#include "util/StringStorage.h"

#include <asio/any_completion_handler.hpp>
#include <asio/any_io_executor.hpp>
#include <asio/async_result.hpp>
#include <asio/ip/tcp.hpp>

#include <system_error>
#include <vector>

//
// Looks up the IPv4 addresses of a host name, asynchronously, for the
// network thread (notes/async-networking.md, "Host name resolution").
//
// Windows' GetAddrInfoExW, rather than Asio's resolver: it takes the name
// in UTF-16, converting an internationalised name to Punycode itself; it
// waits for DNS without blocking a thread of ours; and it can really be
// cancelled (GetAddrInfoExCancel), where Asio's resolver runs the blocking
// lookup on a hidden thread and only discards its result.
//
// The completion signature is void(std::error_code, std::vector of
// endpoints). The endpoints carry port, in the order the system gives
// them; asio::async_connect() tries them in turn.
//
// Cancellation, through the completion handler's cancellation slot (any
// type), cancels the lookup; the handler then receives
// asio::error::operation_aborted, unless the lookup had completed already.
//
// Pre-conditions: called on the thread of executor, the network thread's
// (NetworkThread::getExecutor()), on which the handler is called unless it
// has an executor of its own: so the handler, which frees the lookup's
// state, cannot run before the call has returned. host and port are
// copied, so they need not outlive the call.
//
using ResolveSignature = void(std::error_code, std::vector<asio::ip::tcp::endpoint>);
using ResolveHandler = asio::any_completion_handler<ResolveSignature>;

//
// Starts a lookup, for asyncResolve(). Exposed only so that the template
// below can call it; use asyncResolve().
//
void startResolve(asio::any_io_executor executor, const StringStorage &host,
                  UINT16 port, ResolveHandler handler);

template<class CompletionToken>
auto asyncResolve(const asio::any_io_executor &executor, const StringStorage &host,
                  UINT16 port, CompletionToken &&token)
{
  return asio::async_initiate<CompletionToken, ResolveSignature>(
    [](auto handler, asio::any_io_executor executor, StringStorage host, UINT16 port) {
      startResolve(std::move(executor), host, port, ResolveHandler(std::move(handler)));
    },
    token, executor, host, port);
}

#endif // _HOST_RESOLVER_H_
