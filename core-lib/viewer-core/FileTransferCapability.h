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

#ifndef _FILE_TRANSFER_CAPABILITY_H_
#define _FILE_TRANSFER_CAPABILITY_H_

#include "ft-client-lib/FileTransferRequestSender.h"
#include "ft-client-lib/FileTransferReplyBuffer.h"
#include "ft-client-lib/FileTransferMessageProcessor.h"
#include "ft-client-lib/FileTransferCore.h"

#include "log-writer/LogWriter.h"

#include "ServerMessageListener.h"
#include "CapabilitiesManager.h"

#include <functional>

class RemoteViewerCore;

//
// The file transfer of a connection: its messages, and the operations
// (FileTransferCore), which run on the network thread. The user interface
// starts them with post(), and they report to its FileTransferInterface.
//
class FileTransferCapability final : public ServerMessageListener
{
public:
  FileTransferCapability(Logger *logger = nullptr);
  ~FileTransferCapability() override;

  //
  // This method must called from event onConnected() in CoreEventsAdapter.
  //
  void setOutput(RfbOutputGate *output);

  //
  // Overrides ServerMessageListener::onServerMessage().
  //
  Task<> onServerMessage(UINT32 code, AwaitableReader &reader) override;

  //
  // Pre-conditions: called before core starts (addCapabilities());
  // ftInterface outlives the connection.
  //
  void setInterface(FileTransferInterface *ftInterface);

  //
  // Registers the file-transfer messages with core, and keeps it, to post
  // the operations to its network thread.
  //
  // Pre-conditions: called once, before core starts; core is a sibling
  // member of the same owner, made and destroyed with this object.
  //
  void addCapabilities(RemoteViewerCore &core);

  //
  // Runs command with the file transfer's operations, on the network
  // thread (RemoteViewerCore::postToLoop()), with a claim on the owner that
  // keeps this object alive until then; does nothing once the owner is
  // being deleted. An exception from command is logged. Any thread.
  //
  // Pre-conditions: addCapabilities() has been called.
  //
  void post(std::function<void(FileTransferCore &)> command);

  //
  // The connection has ended: FileTransferCore::onConnectionEnded().
  // Called on the network thread, by the adapter.
  //
  void onConnectionEnded();

protected:
  LogWriter m_logWriter;

  FileTransferRequestSender m_ftRequestSender;
  FileTransferReplyBuffer m_ftReplyBuffer;
  FileTransferMessageProcessor m_ftMessageProcessor;

  FileTransferCore m_ftCore;

  // Set by addCapabilities().
  RemoteViewerCore *m_viewerCore;
};

#endif
