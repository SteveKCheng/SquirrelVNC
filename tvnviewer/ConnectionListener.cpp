// Copyright (C) 2012 GlavSoft LLC.
// All rights reserved.
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

#include "ConnectionListener.h"
#include "TvnViewer.h"

#include "client-config-lib/ViewerConfig.h"

#include <utility>

ConnectionListener::ConnectionListener(WindowsApplication *application,
                                       asio::any_io_executor executor,
                                       UINT16 port)
: m_inbox(std::make_shared<Inbox>()),
  m_acceptor(executor,
             asio::ip::tcp::endpoint(asio::ip::address_v4::any(), port),
             [application, inbox = m_inbox](asio::ip::tcp::socket socket) {
               {
                 std::lock_guard<std::mutex> lock(inbox->lock);
                 inbox->connections.push_back(std::move(socket));
               }
               application->postMessage(TvnViewer::WM_USER_NEW_LISTENING);
             },
             ViewerConfig::getInstance()->getLogger())
{
}

UINT16 ConnectionListener::getBindPort() const
{
  return m_acceptor.getPort();
}

std::optional<asio::ip::tcp::socket> ConnectionListener::getNewConnection()
{
  std::lock_guard<std::mutex> lock(m_inbox->lock);
  std::optional<asio::ip::tcp::socket> socket;
  if (!m_inbox->connections.empty()) {
    socket.emplace(std::move(m_inbox->connections.front()));
    m_inbox->connections.pop_front();
  }
  return socket;
}
