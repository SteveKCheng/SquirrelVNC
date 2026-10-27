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

#ifndef _FILE_TRANSFER_INTERFACE_H_
#define _FILE_TRANSFER_INTERFACE_H_

#include "FileTransferCore.h"

class FileTransferCore;

//
// What the file transfer reports to its user interface. FileTransferCore
// calls it from the operations, on the network thread (and a local delete's
// own thread), and from the calls the dialog makes into the core, on the GUI
// thread; each function says which.
//
class FileTransferInterface
{
public:
  virtual ~FileTransferInterface();

  //
  // From an operation, on the network thread: what to do with a target
  // file that exists already. Returns one of CopyFileEventListener's TFE_*
  // values.
  //
  virtual int onFtTargetFileExists(FileInfo *sourceFileInfo,
                                   FileInfo *targetFileInfo,
                                   const TCHAR *pathToTargetFile) = 0;
  //
  // From an operation, on the network thread.
  // Progress is in interval [0.0, 1.0].
  //
  virtual void setProgress(double progress) = 0;

  //
  // This function inform user about error.
  // From an operation, on any thread; it must not block.
  //
  virtual void onFtOpError(const TCHAR *message) = 0;

  //
  // This function inform user additional information.
  // From an operation, on any thread; it must not block.
  //
  virtual void onFtOpInfo(const TCHAR *message) = 0;

  //
  // From an operation, on any thread, when it starts and when it finishes
  // (state is FileTransferCore's, result is 1 for a successful file list,
  // otherwise 0). They must not block.
  //
  virtual void onFtOpStarted() = 0;
  virtual void onFtOpFinished(int state, int result) = 0;

  //
  // The rest are called on the GUI thread, from FileTransferCore functions
  // that the dialog calls.
  //

  //
  // filetransfer's operation is finished. Need update of control
  //
  virtual void setNothingState() = 0;

  //
  // Called if local file list is updated
  //
  virtual void onRefreshLocalFileList() = 0;

  // Called if remote file list is updated
  virtual void onRefreshRemoteFileList() = 0;

  //
  // Shows error message and throws exception
  //
  // Pre-conditions: called in a handler that caught ex.
  // Post-conditions: throws: ex itself is rethrown, not a copy of its
  // Exception part.
  //

  virtual void raise(const Exception &ex) = 0;

};

#endif
