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
                               const ConnectionData &condata,
                               const ConnectionConfig &conConf)
: m_conConf(conConf),
  m_condata(condata),
  m_viewerWnd(application,
              &m_condata,
              &m_conConf,
              ViewerConfig::getInstance()->getLogger()),
  m_vncAuthHandler(&m_condata, m_viewerWnd),
  m_viewerCore(ViewerConfig::getInstance()->getLogger())
{
}

ViewerInstance::ViewerInstance(WindowsApplication *application,
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
  m_vncAuthHandler(&m_condata, m_viewerWnd),
  m_viewerCore(ViewerConfig::getInstance()->getLogger())
{
}


ViewerInstance::~ViewerInstance()
{
  if (m_socket) {
    // shutdown() throws if it fails (the connection may be gone), and
    // nothing may leave a destructor.
    try {
      m_socket->shutdown(SD_BOTH);
    } catch (...) {
    }
    m_socket->close();
  }

  m_viewerCore.stop();
  // The core thread may be waiting for the password from this thread.
  m_viewerWnd.cancelPasswordRequest();
  m_viewerCore.waitTermination();
}

void ViewerInstance::waitViewer()
{
  // The core thread may be waiting for the password from this thread.
  m_viewerWnd.cancelPasswordRequest();
  m_viewerCore.waitTermination();
}

bool ViewerInstance::requiresReconnect() const
{
  return m_viewerWnd.requiresReconnect();
}

bool ViewerInstance::isStopped() const
{
  return m_viewerWnd.isStopped();
}

void ViewerInstance::stop()
{
  m_viewerWnd.postMessage(ViewerWindow::WM_USER_STOP);
}

void ViewerInstance::start()
{
  m_viewerWnd.setRemoteViewerCore(&m_viewerCore);


  m_viewerWnd.setFileTransfer(&m_fileTransfer);

  m_vncAuthHandler.addAuthCapability(m_viewerCore);

  m_fileTransfer.addCapabilities(&m_viewerCore);

  if (m_socket) {
    m_viewerCore.start(m_socket.get(),
                       &m_viewerWnd, m_conConf.getSharedFlag());
  } else {
    StringStorage strHost;
    m_condata.getReducedHost(&strHost);
    // HostPath only accepts ports from 0 to 65535.
    UINT16 portVal = static_cast<UINT16>(m_condata.getPort());
    m_viewerCore.start(strHost.getString(), portVal,
                       &m_viewerWnd, m_conConf.getSharedFlag());
  }
}
