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

#ifndef _FILE_TRANSFER_EVENT_HANDLER_H_
#define _FILE_TRANSFER_EVENT_HANDLER_H_

//
// Abstract class with abstract methods that must be
// overrided by child classes to process file transfer
// extension messages.
//
// FIXME: It can be renamed to FileTransferMessageListener
//
// Each method is called on the core thread, after the reply has been read
// in full by FileTransferReplyBuffer, which holds its contents; so a
// handler does not read the stream. If reading the reply fails, no handler
// is called for it. Called with the listener list of
// FileTransferMessageProcessor locked.
//

class FileTransferEventHandler
{
public:
  FileTransferEventHandler();
  virtual ~FileTransferEventHandler();

  virtual void onCompressionSupportReply() = 0;
  virtual void onFileListReply() = 0;
  virtual void onMd5DataReply() = 0;

  virtual void onUploadReply() = 0;
  virtual void onUploadDataReply() = 0;
  virtual void onUploadEndReply() = 0;

  virtual void onDownloadReply() = 0;
  virtual void onDownloadDataReply() = 0;
  virtual void onDownloadEndReply() = 0;

  virtual void onMkdirReply() = 0;
  virtual void onRmReply() = 0;
  virtual void onMvReply() = 0;

  virtual void onDirSizeReply() = 0;
  virtual void onLastRequestFailedReply() = 0;
};

#endif
