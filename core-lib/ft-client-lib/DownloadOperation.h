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

#ifndef _DOWNLOAD_OPERATION_H_
#define _DOWNLOAD_OPERATION_H_

#include "ft-common/FileInfo.h"
#include "file-lib/WinFileChannel.h"
#include "FileInfoList.h"
#include "CopyOperation.h"
#include "util/DateTime.h"
#include "file-lib/File.h"

#include <memory>

//
// File transfer operation class for downloading files (and file trees).
//

class DownloadOperation final : public CopyOperation
{
public:

  //
  // Parameters:
  //
  // [IN] filesToDownload - array of files info to be downloaded
  // [IN] filesCount - count elements in filesToDownload array
  // [IN] pathToTargetRoot - full path to local folder where downloaded files
  // will be located (system depended).
  // [IN] pathToSourceRoot - full path to remote folder where files to download
  // is located (system independed, see ft protocol info)
  //

  DownloadOperation(LogWriter *logWriter,
                    const FileInfo *filesToDownload, size_t filesCount,
                    const TCHAR *pathToTargetRoot,
                    const TCHAR *pathToSourceRoot);

  ~DownloadOperation() override;

  //
  // Inherited from FileTransferOperation
  //

  void start() override;

protected:

  //
  // Inherited from FileTransferEventHandler
  //

  void onFileListReply(DataInputStream *input) override;
  void onDownloadReply(DataInputStream *input) override;
  void onDownloadDataReply(DataInputStream *input) override;
  void onDownloadEndReply(DataInputStream *input) override;
  void onLastRequestFailedReply(DataInputStream *input) override;
  void onDirSizeReply(DataInputStream *input) override;

private:

  // Starts download of current file (m_toCopy member)
  void startDownload();

  // Starts download of file
  void processFile();

  // Start download of folder
  void processFolder();

  // Sets m_toCopy member to next file to download
  void gotoNext();

  // Sends get folder size request to server to know
  // how many bytes must be receivied during
  // all download process
  void tryCalcInputFilesSize();

  // Terminates operation execution
  void killOp();

  // Helper method that decrements m_foldersToCalcSizeLeft count
  // and if it's equal to null, start first file download
  void decFoldersToCalcSizeCount();

  // Helper method that creates message string and notifies listeners
  void notifyFailedToDownload(const TCHAR *errorDescription);

  // Sets current m_toCopy member value and updates
  // m_pathToSourceFile, m_pathToTargetFile members
  void changeFileToDownload(FileInfoList *toDownload);

  //
  // Post-conditions: the file being downloaded, if any, is closed (errors in
  // closing it are ignored), and m_fos and m_file are empty.
  //
  void closeFile();

protected:
  // Target local file
  std::unique_ptr<File> m_file;
  // File output stream associated with m_file
  std::unique_ptr<WinFileChannel> m_fos;

  // Initial file offset for current download (broken downloads)
  UINT64 m_fileOffset;

  // Helper member to know how many folders to download left
  // to get their file size
  UINT32 m_foldersToCalcSizeLeft;

  // request data size changes dynamicaly depends on request rate
  size_t m_bufferSize;
  DateTime m_lastRequestTime;

};

#endif
