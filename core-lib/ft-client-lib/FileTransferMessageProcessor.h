// Copyright (C) 2009,2010,2011,2012 GlavSoft LLC.
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

#ifndef _FILE_TRANSFER_MESSAGE_PROCESSOR_H_
#define _FILE_TRANSFER_MESSAGE_PROCESSOR_H_

#include "io-lib/IOException.h"
#include "io-lib/DataInputStream.h"

#include "util/inttypes.h"
#include "util/ListenerContainer.h"
#include "util/ZLibException.h"

#include "ft-common/OperationNotSupportedException.h"

#include "OperationNotPermittedException.h"
#include "FileTransferEventHandler.h"
#include "FileTransferReplyBuffer.h"

class FileTransferMessageProcessor final : public ListenerContainer<FileTransferEventHandler *>
{
public:
  //
  // replyBuffer reads the replies; it must outlive this object.
  //
  explicit FileTransferMessageProcessor(FileTransferReplyBuffer &replyBuffer);
  ~FileTransferMessageProcessor();

  //
  // Processes one file-transfer reply, on the core thread: first reads it
  // in full with the reply buffer, then notifies the listeners, which do
  // not read the stream. Reading is done without the listener list locked.
  //
  // Pre-conditions: the message type, rfbMessage, has been read from input.
  // Post-conditions: the reply has been read from input, and each listener
  // notified of it.
  // Throws whatever FileTransferReplyBuffer::readReply() throws, before any
  // listener is notified; and whatever a listener throws, after which the
  // listeners after it are not notified.
  //
  void processRfbMessage(DataInputStream *input, UINT32 rfbMessage);

private:
  FileTransferReplyBuffer &m_replyBuffer;
};

#endif
