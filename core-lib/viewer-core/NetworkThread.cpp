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

#include "NetworkThread.h"

#include <cassert>
#include <stdexcept>

NetworkThread *NetworkThread::s_instance = nullptr;

NetworkThread::NetworkThread()
: m_pool(1)
{
  assert(s_instance == nullptr);
  s_instance = this;
}

NetworkThread::~NetworkThread()
{
  join();
  s_instance = nullptr;
}

NetworkThread &NetworkThread::instance()
{
  if (s_instance == nullptr) {
    throw std::logic_error("NetworkThread::instance(): no NetworkThread exists");
  }
  return *s_instance;
}

asio::any_io_executor NetworkThread::getExecutor() noexcept
{
  return m_pool.get_executor();
}

void NetworkThread::join()
{
  m_pool.join();
}
