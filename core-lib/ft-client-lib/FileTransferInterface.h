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

#include "ft-common/FileInfo.h"

#include <vector>

//
// What the file transfer reports to its user interface. FileTransferCore
// calls it on the network thread, and a local delete on its own thread;
// each function says which. None may block.
//
class FileTransferInterface
{
public:
  virtual ~FileTransferInterface();

  //
  // From an operation, on the network thread: asks the user what to do
  // with targetFileInfo, a target file that exists already, without
  // waiting. The answer, one of CopyFileEventListener's TFE_* values, is
  // given later to FileTransferCore::answerTargetFileExists(), on the
  // network thread.
  //
  // Post-conditions: returns whether the question was asked; if not, no
  // answer will come.
  //
  virtual bool onFtTargetFileExists(const FileInfo &sourceFileInfo,
                                    const FileInfo &targetFileInfo,
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
  // On the network thread: the remote folder listed by a file-list
  // operation, just before its successful end (onFtOpFinished() with
  // FILE_LIST_STATE and a result of 1).
  //
  virtual void onFtFileList(std::vector<FileInfo> files) = 0;

  //
  // On the network thread: the connection has ended. No reply will come;
  // an operation on the server does not report its end.
  //
  virtual void onFtConnectionEnded() = 0;
};

#endif
