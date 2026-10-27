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

#include "ViewerInstance.h"

#include "viewer-core/RemoteViewerCore.h"
#include "viewer-core/FileTransferCapability.h"

ViewerInstance::ViewerInstance(WindowsApplication *application,
                               asio::any_io_executor executor,
                               BlockingWorkPool &blockingWorkPool,
                               const ConnectionData &condata,
                               const ConnectionConfig &conConf)
: m_conConf(conConf),
  m_condata(condata),
  m_viewerWnd(application,
              &m_condata,
              &m_conConf,
              ViewerConfig::getInstance()->getLogger()),
  m_vncAuthHandler(&m_condata, m_viewerWnd.getCoreEvents()),
  m_viewerCore(std::move(executor), ViewerConfig::getInstance()->getLogger())
{
  m_fileTransfer.setBlockingWorkPool(blockingWorkPool);
}

ViewerInstance::ViewerInstance(WindowsApplication *application,
                               asio::any_io_executor executor,
                               BlockingWorkPool &blockingWorkPool,
                               const ConnectionData &condata,
                               const ConnectionConfig &conConf,
                               std::unique_ptr<SocketIPv4> socket)
: m_conConf(conConf),
  m_condata(condata),
  m_socket(std::move(socket)),
  m_viewerWnd(application,
              &m_condata,
              &m_conConf,
              ViewerConfig::getInstance()->getLogger()),
  m_vncAuthHandler(&m_condata, m_viewerWnd.getCoreEvents()),
  m_viewerCore(std::move(executor), ViewerConfig::getInstance()->getLogger())
{
  m_fileTransfer.setBlockingWorkPool(blockingWorkPool);
}


ViewerInstance::~ViewerInstance()
{
  // An accepted socket belongs to the core since start().
  m_viewerWnd.destroy();
}

bool ViewerInstance::requiresReconnect() const
{
  return m_viewerWnd.requiresReconnect();
}

void ViewerInstance::stop()
{
  m_viewerWnd.destroy();
}

void ViewerInstance::start()
{
  m_viewerWnd.setRemoteViewerCore(&m_viewerCore);


  m_viewerWnd.setFileTransfer(&m_fileTransfer);

  m_vncAuthHandler.addAuthCapability(m_viewerCore);

  m_fileTransfer.addCapabilities(m_viewerCore);

  // The core sees only the adapter, but through this object's control
  // block, which keeps the core and what is registered with it alive.
  std::shared_ptr<ViewerInstance> self = shared_from_this();
  std::shared_ptr<CoreEventsAdapter> adapter = m_viewerWnd.getCoreEvents().getAdapter(self);
  if (m_socket) {
    m_viewerCore.start(m_socket.get(),
                       std::move(adapter), m_conConf.getSharedFlag());
  } else {
    StringStorage strHost;
    m_condata.getReducedHost(&strHost);
    // HostPath only accepts ports from 0 to 65535.
    UINT16 portVal = static_cast<UINT16>(m_condata.getPort());
    m_viewerCore.start(strHost.getString(), portVal,
                       std::move(adapter), m_conConf.getSharedFlag());
  }
  // Last, so that a window whose connection failed to start does not keep
  // the instance alive.
  m_viewerWnd.setOwner(std::move(self));
}
