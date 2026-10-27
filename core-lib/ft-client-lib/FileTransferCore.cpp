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

#include "FileTransferCore.h"

#include "RemoteFileListOperation.h"
#include "RemoteFilesDeleteOperation.h"
#include "RemoteFolderCreateOperation.h"
#include "RemoteFileRenameOperation.h"
#include "LocalFilesDeleteOperation.h"
#include "UploadOperation.h"
#include "DownloadOperation.h"

#include <vector>

FileTransferCore::FileTransferCore(LogWriter *logWriter,
                                   FileTransferRequestSender *sender,
                                   FileTransferReplyBuffer *replyBuffer,
                                   ListenerContainer<FileTransferEventHandler *> *ftListeners,
                                   BlockingWorkRunner &blockingWorkRunner)
: m_logWriter(logWriter),
  m_state(NOTHING_STATE),
  m_sender(sender), m_replyBuffer(replyBuffer),
  m_blockingWorkRunner(blockingWorkRunner),
  m_fileTransferListeners(ftListeners),
  m_ftInterface(nullptr)
{
}

FileTransferCore::~FileTransferCore()
{
  // Not left in the listeners when it is freed.
  if (m_currentOperation) {
    m_fileTransferListeners->removeListener(m_currentOperation.get());
  }
}

void FileTransferCore::dataChunkCopied(UINT64 totalBytesCopied, UINT64 totalBytesToCopy)
{
  if (m_state == NOTHING_STATE) {
    return;
  }

  if (totalBytesToCopy != 0) {
    double progress = 1.0 * totalBytesCopied / totalBytesToCopy;

    // event set pos to progress bar
    m_ftInterface->setProgress(progress);
  }
}

bool FileTransferCore::targetFileExists(const FileInfo &sourceFileInfo,
                                        const FileInfo &targetFileInfo,
                                        const TCHAR *pathToTargetFile)
{
  return m_ftInterface->onFtTargetFileExists(sourceFileInfo,
                                             targetFileInfo,
                                             pathToTargetFile);
}

void FileTransferCore::setInterface(FileTransferInterface *ftInterface)
{
  m_ftInterface = ftInterface;
}

void FileTransferCore::ftOpStarted(FileTransferOperation * /* sender */)
{
  m_ftInterface->onFtOpStarted();
}

void FileTransferCore::ftOpFinished(FileTransferOperation *sender)
{
  //
  // If operation that sends to us this notification is remote file list operation
  // than we need to copy received file list to class members.
  //

  if (m_state == FILE_LIST_STATE) {
    RemoteFileListOperation *fileListOp = dynamic_cast<RemoteFileListOperation *>(sender);

    if (fileListOp->isOk()) {
      // A copy for the dialog: the reply buffer's list lasts until the
      // next reply.
      const FileInfo *files = m_replyBuffer->getFilesInfo();
      m_ftInterface->onFtFileList(std::vector<FileInfo>(files,
                                                        files + m_replyBuffer->getFilesInfoCount()));
    } // if no error during file list operation

    // Notify dialog than operation is finished
    int result = fileListOp->isOk() ? 1 : 0;
    m_ftInterface->onFtOpFinished(m_state, result);
    return ;
  } // if last executed operation was file list opearation

  //
  // The operation is not removed from the message processor's listeners
  // here: this may be called while the processor iterates over them,
  // notifying the operation of a reply. The dialog, once it has handled
  // the end, posts onOperationFinished(), which removes it.
  //

  m_ftInterface->onFtOpFinished(m_state, 0);
} // void

void FileTransferCore::ftOpErrorMessage(FileTransferOperation * /* sender */,
                                        const TCHAR *message)
{
  m_ftInterface->onFtOpError(message);
}

void FileTransferCore::ftOpInfoMessage(FileTransferOperation * /* sender */,
                                       const TCHAR *message)
{
  m_ftInterface->onFtOpInfo(message);
}

void FileTransferCore::executeOperation(std::unique_ptr<FileTransferOperation> newOperation)
{
  //
  // Cleanup, free previous operation
  //

  if (m_currentOperation) {
    m_currentOperation->removeListener(this);
    m_fileTransferListeners->removeListener(m_currentOperation.get());
  }

  m_currentOperation = std::move(newOperation);

  m_currentOperation->setRequestSender(m_sender);
  m_currentOperation->setReplyBuffer(m_replyBuffer);
  m_currentOperation->addListener(this);

  m_fileTransferListeners->addListener(m_currentOperation.get());

  try {
    m_currentOperation->start();
  } catch (const IOException &ioEx) {
    // The operation can do nothing more: no reply will come.
    m_ftInterface->onFtOpError(ioEx.getMessage());
    m_ftInterface->onFtOpFinished(m_state, 0);
  } // try / catch
}

void FileTransferCore::downloadOperation(const FileInfo *filesToDownload,
                                         size_t filesCount,
                                         const TCHAR *pathToTargetRoot,
                                         const TCHAR *pathToSourceRoot)
{
  m_state = DOWNLOAD_STATE;

  std::unique_ptr<DownloadOperation> dOp =
    std::make_unique<DownloadOperation>(m_logWriter,
                                        filesToDownload,
                                        filesCount,
                                        pathToTargetRoot,
                                        pathToSourceRoot);
  dOp->setCopyProcessListener(this);
  executeOperation(std::move(dOp));
}

void FileTransferCore::uploadOperation(const FileInfo *filesToDownload,
                                       size_t filesCount,
                                       const TCHAR *pathToSourceRoot,
                                       const TCHAR *pathToTargetRoot)
{
  m_state = UPLOAD_STATE;

  std::unique_ptr<UploadOperation> uOp =
    std::make_unique<UploadOperation>(m_logWriter,
                                      filesToDownload,
                                      filesCount,
                                      pathToSourceRoot,
                                      pathToTargetRoot);
  uOp->setCopyProcessListener(this);
  executeOperation(std::move(uOp));
}

void FileTransferCore::localFilesDeleteOperation(const FileInfo *filesToDelete,
                                                 size_t filesCount,
                                                 const TCHAR *pathToTargetRoot)
{
  m_state = LOCAL_REMOVE_STATE;
  executeOperation(std::make_unique<LocalFilesDeleteOperation>(m_logWriter,
                                                               m_blockingWorkRunner,
                                                               filesToDelete, filesCount,
                                                               pathToTargetRoot));
}

void FileTransferCore::remoteFilesDeleteOperation(const FileInfo *filesInfoToDelete,
                                                 size_t filesCount,
                                                 const TCHAR *pathToTargetRoot)
{
  m_state = REMOVE_STATE;
  executeOperation(std::make_unique<RemoteFilesDeleteOperation>(m_logWriter,
                                                                filesInfoToDelete, filesCount,
                                                                pathToTargetRoot));
}

void FileTransferCore::remoteFolderCreateOperation(FileInfo file, const TCHAR *pathToTargetRoot)
{
    m_state = MKDIR_STATE;

    executeOperation(std::make_unique<RemoteFolderCreateOperation>(m_logWriter,
                                                                   file,
                                                                   pathToTargetRoot));
}
void FileTransferCore::remoteFileRenameOperation(FileInfo sourceFileInfo,
                                                 FileInfo targetFileInfo,
                                                 const TCHAR *pathToTargetRoot)
{
  m_state = RENAME_STATE;

  executeOperation(std::make_unique<RemoteFileRenameOperation>(m_logWriter,
                                                               sourceFileInfo,
                                                               targetFileInfo,
                                                               pathToTargetRoot));
}

void FileTransferCore::remoteFileListOperation(const TCHAR *pathToFile)
{
  m_state = FILE_LIST_STATE;
  executeOperation(std::make_unique<RemoteFileListOperation>(m_logWriter, pathToFile));
}

void FileTransferCore::terminateCurrentOperation()
{
  if (m_currentOperation) {
    m_currentOperation->terminate();
  }
}

void FileTransferCore::answerTargetFileExists(int action)
{
  if (CopyOperation *copy = dynamic_cast<CopyOperation *>(m_currentOperation.get())) {
    copy->answerTargetFileExists(action);
  }
}

void FileTransferCore::onOperationFinished()
{
  // Operation is finished, remove it from file transfer message handlers
  if (m_currentOperation) {
    m_fileTransferListeners->removeListener(m_currentOperation.get());
  }
  m_state = NOTHING_STATE;
}

void FileTransferCore::onConnectionEnded()
{
  // A local delete needs no connection, and finishes by itself.
  if (m_currentOperation && m_state != NOTHING_STATE &&
      m_state != LOCAL_REMOVE_STATE) {
    m_currentOperation->terminate();
  }
  m_ftInterface->onFtConnectionEnded();
}
