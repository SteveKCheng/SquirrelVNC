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

#ifndef __VIEWER_INSTANCE_H__
#define __VIEWER_INSTANCE_H__

#include "ViewerWindow.h"
#include "network/socket/SocketIPv4.h"

#include "ViewerVncAuthHandler.h"


class ViewerInstance final
{
public:
  // creates the viewer instance by using host:port from condata
  //
  // Both constructors copy condata and conConf. The second takes
  // ownership of socket.
  ViewerInstance(WindowsApplication *application,
                 const ConnectionData &condata,
                 const ConnectionConfig &conConf);

  // creates the viewer instance if we have the socket
  ViewerInstance(WindowsApplication *application,
                 const ConnectionData &condata,
                 const ConnectionConfig &conConf,
                 std::unique_ptr<SocketIPv4> socket);

  ViewerInstance(const ViewerInstance &) = delete;
  ViewerInstance &operator=(const ViewerInstance &) = delete;
  ViewerInstance(ViewerInstance &&) = delete;
  ViewerInstance &operator=(ViewerInstance &&) = delete;

  ~ViewerInstance();

  bool requiresReconnect() const;
  bool isStopped() const;
  void start();
  void stop();

  // wait while viewer is not terminated
  void waitViewer();

protected:
  ConnectionData m_condata;
  ConnectionConfig m_conConf;
  // The connection accepted when listening, or empty. Declared before the
  // core, which uses it, so that it outlives the core.
  std::unique_ptr<SocketIPv4> m_socket;

  ViewerWindow m_viewerWnd;
  RemoteViewerCore m_viewerCore;
  ViewerVncAuthHandler m_vncAuthHandler;
  FileTransferCapability m_fileTransfer;
};

#endif
