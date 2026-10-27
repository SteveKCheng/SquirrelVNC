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

#ifndef _FILE_TRANSFER_CORE_H_
#define _FILE_TRANSFER_CORE_H_

#include "ft-common/FileInfo.h"
#include "log-writer/LogWriter.h"

#include "FileTransferEventHandler.h"
#include "FileTransferRequestSender.h"
#include "FileTransferReplyBuffer.h"
#include "FileTransferOperation.h"
#include "OperationEventListener.h"
#include "CopyFileEventListener.h"

#include "FileTransferInterface.h"

#include <memory>

class FileTransferInterface;

//
// Runs the file-transfer operations, one at a time, and reports them to its
// FileTransferInterface.
//
// Threads: every function is called on the network thread, where the
// replies are dispatched; the dialog posts its commands there
// (FileTransferCapability::post()). The exception, until it moves to a
// pool (notes/async-networking.md, Phase 4 step 5d): a local delete runs on
// a thread of its own, from which it reports its messages and its end.
//
class FileTransferCore final : public OperationEventListener,
                         public CopyFileEventListener
{
public:
  //
  // The kind of operation last started, as given to
  // FileTransferInterface::onFtOpFinished().
  // FIXME: Maybe make m_state enumeration?
  //
  static const int NOTHING_STATE      = 0x0;
  static const int FILE_LIST_STATE    = 0x1;
  static const int REMOVE_STATE       = 0x2;
  static const int MKDIR_STATE        = 0x3;
  static const int RENAME_STATE       = 0x4;
  static const int LOCAL_REMOVE_STATE = 0x5;
  static const int UPLOAD_STATE       = 0x6;
  static const int DOWNLOAD_STATE     = 0x7;

  //
  // Creates new instance of class.
  //
  // Parameters:
  // sender - object that needs for file transfer operations
  // replyBuffer - object that needs for file transfer operations
  // ftListeners - needs for executing file transfer operations
  //

  FileTransferCore(LogWriter *logWriter,
                   FileTransferRequestSender *sender,
                   FileTransferReplyBuffer *replyBuffer,
                   ListenerContainer<FileTransferEventHandler *> *ftListeners);
  FileTransferCore(const FileTransferCore &) = delete;
  FileTransferCore &operator=(const FileTransferCore &) = delete;
  FileTransferCore(FileTransferCore &&) = delete;
  FileTransferCore &operator=(FileTransferCore &&) = delete;

  ~FileTransferCore();

  //
  // Pre-conditions: called before any operation is started.
  //
  void setInterface(FileTransferInterface *ftInterface);

  //
  // Start an operation, freeing the previous one. The operation reports
  // its start and its end to the interface; if it cannot start, because
  // the connection is closed, it reports the error and its end at once.
  //
  // Pre-conditions: the previous operation, if any, has finished. The
  // arguments are copied.
  //
  void downloadOperation(const FileInfo *filesToDownload,
                         size_t filesCount,
                         const TCHAR *pathToTargetRoot,
                         const TCHAR *pathToSourceRoot);
  void uploadOperation(const FileInfo *filesToDownload,
                       size_t filesCount,
                       const TCHAR *pathToSourceRoot,
                       const TCHAR *pathToTargetRoot);
  void localFilesDeleteOperation(const FileInfo *filesToDelete,
                                 size_t filesCount,
                                 const TCHAR *pathToTargetRoot);
  void remoteFilesDeleteOperation(const FileInfo *filesInfoToDelete,
                                  size_t filesCount,
                                  const TCHAR *pathToTargetRoot);
  void remoteFolderCreateOperation(FileInfo file, const TCHAR *pathToTargetRoot);
  void remoteFileRenameOperation(FileInfo sourceFileInfo,
                                 FileInfo targetFileInfo,
                                 const TCHAR *pathToTargetRoot);
  void remoteFileListOperation(const TCHAR *pathToFile);

  //
  // Asks the current operation, if any, to stop; it finishes as it would
  // otherwise, reporting its end.
  //
  void terminateCurrentOperation();

  //
  // The user's answer to FileTransferInterface::onFtTargetFileExists(),
  // one of CopyFileEventListener's TFE_* values, for the current operation
  // (CopyOperation::answerTargetFileExists()); ignored if the current
  // operation asked nothing.
  //
  void answerTargetFileExists(int action);

  //
  // For the dialog, once it has handled the end of the operation (posted
  // in reply to FileTransferInterface::onFtOpFinished()): the operation
  // gets no more replies.
  //
  // Post-conditions: no operation is running (NOTHING_STATE).
  //
  void onOperationFinished();

  //
  // The connection has ended: the current operation, unless it is a local
  // delete, is asked to stop, and the interface is told
  // (onFtConnectionEnded()). No reply will come any more, so the operation
  // does not report its end.
  //
  void onConnectionEnded();

protected:
  //
  // Inherited from CopyFileEventListener
  //

  void dataChunkCopied(UINT64 totalBytesCopied, UINT64 totalBytesToCopy) override;

  // Asks through the interface (onFtTargetFileExists()).
  bool targetFileExists(const FileInfo &sourceFileInfo,
                        const FileInfo &targetFileInfo,
                        const TCHAR *pathToTargetFile) override;

  //
  // Inherited from OperationEventListener
  //

  void ftOpStarted(FileTransferOperation *sender) override;
  void ftOpFinished(FileTransferOperation *sender) override;
  void ftOpErrorMessage(FileTransferOperation *sender, const TCHAR *message) override;
  void ftOpInfoMessage(FileTransferOperation *sender,
                       const TCHAR *message) override;

  //
  // Interface of log
  //

  LogWriter *m_logWriter;

  //
  // Current dialog state, needed for checking what type of
  // file transfer operation is executing now (means nothing or
  // delete, upload, download etc).
  //

  int m_state;

  //
  // Current file transfer operation.
  // Remark: only one file operation can be executed at one time
  //

  std::unique_ptr<FileTransferOperation> m_currentOperation;
  //
  // File transfer message listener container.
  //
  // This member needs for executing file transfer operations,
  // after operation is created, but before it has started it needs
  // to be added to these listeners, after it has finished work needs
  // to remove operation from listeners list.
  //

  ListenerContainer<FileTransferEventHandler *> *m_fileTransferListeners;
  //
  // Request sender, reply buffer
  //

  FileTransferRequestSender *m_sender;
  FileTransferReplyBuffer *m_replyBuffer;

  //
  // file transfer interface
  //
  FileTransferInterface *m_ftInterface;

  //
  // Frees previous file transfer operation if it was, sets
  // common file transfer operation parameters to new operation, made
  // new operation current and executes it. If it cannot start (an
  // IOException: the connection is closed), the error and the end of the
  // operation are reported to the interface.
  //
  // Pre-conditions: newOperation is not empty.
  //

  void executeOperation(std::unique_ptr<FileTransferOperation> newOperation);

};

#endif
