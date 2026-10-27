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

#ifndef _COPY_OPERATION_H_
#define _COPY_OPERATION_H_

#include "FileTransferOperation.h"
#include "CopyFileEventListener.h"
#include "FileInfoList.h"

#include <memory>

//
// Abstract class that contain intersection of functionality
// of Download and Upload operations.
//

class CopyOperation : public FileTransferOperation
{
protected:
  // Protected: only a subclass makes one.
  CopyOperation(LogWriter *logWriter);

public:
  ~CopyOperation() override;

  // Sets copy event listener
  void setCopyProcessListener(CopyFileEventListener *listener);

  //
  // The answer to the question of askTargetFileExists(), one of
  // CopyFileEventListener's TFE_* values: the operation goes on
  // (onTargetFileExistsAnswer()). Does nothing unless a question is
  // pending: an answer that comes late, or unasked, is ignored.
  //
  // Post-conditions: no question is pending.
  //
  void answerTargetFileExists(int action);

protected:
  //
  // Asks the copy listener what to do with targetFileInfo, the existing
  // target of the current file, sourceFileInfo, keeping a copy of it in
  // m_existingTargetInfo. If the question was asked, the caller returns
  // without sending anything, and the operation waits for the answer; if
  // not, no answer will come, and the caller stops the operation as on a
  // cancel.
  //
  // Pre-conditions: no question is pending; m_copyListener is set.
  // Post-conditions: returns whether the question was asked, and so is
  // pending.
  //
  bool askTargetFileExists(const FileInfo &sourceFileInfo,
                           const FileInfo &targetFileInfo);

  //
  // Whether a question of askTargetFileExists() is waiting for its answer.
  //
  bool isAskingTargetFileExists() const;

  //
  // Goes on with the current file after the answer to
  // askTargetFileExists(). A cancel, or an answer to an operation that is
  // terminating, finishes the operation.
  //
  virtual void onTargetFileExistsAnswer(int action) = 0;

  // The existing target of the last question, as asked.
  FileInfo m_existingTargetInfo;

  // The tree of files to copy, which owns all of it, and the file that is
  // currently being copied in it (0 once the operation has ended).
  std::unique_ptr<FileInfoList> m_toCopyRoot;
  FileInfoList *m_toCopy;

  // Path to root folder where files that should be copied is located
  StringStorage m_pathToSourceRoot;
  // Path to target root folder where copied files will be located
  // after coping
  StringStorage m_pathToTargetRoot;

  // Path to source file that we currently coping
  StringStorage m_pathToSourceFile;
  // Path to target file in that we currently coping
  // source file
  StringStorage m_pathToTargetFile;

  // Listener of copy operation events
  CopyFileEventListener *m_copyListener;

  // Members that needed to know how many bytes of data transfered
  // and how many left to copy
  UINT64 m_totalBytesToCopy;
  UINT64 m_totalBytesCopied;

private:
  bool m_isAskingTargetFileExists;
};

#endif
