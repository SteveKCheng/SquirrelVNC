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

#include <memory>

//
// A viewer window and its connection. Owned by std::shared_ptr
// (notes/async-networking.md, "Ownership by std::shared_ptr"): from start(),
// the window holds a reference until it is destroyed, and the connection
// until it has ended, so the instance lives as long as either. The last
// reference deletes it on the GUI thread (ViewerCollector::addInstance()).
//
class ViewerInstance final : public std::enable_shared_from_this<ViewerInstance>
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

  //
  // Destroys the window if it is still there, which happens only if start()
  // was not called or failed; the connection has ended, as it held a
  // reference until then. Called by the deleter of ViewerCollector, on the
  // GUI thread, at the top of the message loop.
  //
  ~ViewerInstance();

  bool requiresReconnect() const;

  //
  // Starts the connection, which holds a reference to this object until it
  // has ended (RemoteViewerCore::start(), given the window's
  // ViewerCoreEvents as the adapter, through this object's control block),
  // and then gives the window its own
  // reference (ViewerWindow::setOwner()). GUI thread.
  //
  // Pre-conditions: owned by a std::shared_ptr (ViewerCollector::
  // addInstance()); called once.
  // Post-conditions: on an exception, the connection was not started, and
  // the window holds no reference, so the instance goes with the caller's.
  //
  void start();

  //
  // For cooperative termination: destroys the window, which stops the
  // connection and cancels a pending password request. Does not wait; the
  // instance is deleted once the connection has ended.
  //
  // Pre-conditions: GUI thread, with no window procedure of this window on
  // the stack (ViewerWindow::destroy()).
  //
  void stop();

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
