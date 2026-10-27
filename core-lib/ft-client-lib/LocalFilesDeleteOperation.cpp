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

#include "LocalFilesDeleteOperation.h"

LocalFilesDeleteOperation::LocalFilesDeleteOperation(LogWriter *logWriter,
                                                     BlockingWorkRunner &runner,
                                                     const FileInfo *filesToDelete,
                                                     size_t filesCount,
                                                     const TCHAR *pathToTargetRoot)
: FileTransferOperation(logWriter),
  m_runner(runner),
  m_isStopRequested(false)
{
  m_filesToDelete.assign(filesToDelete, filesToDelete + filesCount);
  m_pathToTargetRoot.setString(pathToTargetRoot);
}

LocalFilesDeleteOperation::~LocalFilesDeleteOperation()
{
}

void LocalFilesDeleteOperation::start()
{
  // Notify listeners that operation have started
  notifyStart();

  // The runner keeps this operation alive until the deletion, and the
  // reports it posts, have run.
  if (!m_runner.runBlocking([this] { deleteFiles(); })) {
    notifyError(_T("Error: cannot delete local files now"));
    notifyFinish();
  }
}

void LocalFilesDeleteOperation::terminate()
{
  FileTransferOperation::terminate();
  m_isStopRequested = true;
}

void LocalFilesDeleteOperation::report(const StringStorage &message, bool isError)
{
  m_runner.postToNetwork([this, message, isError] {
    if (isError) {
      notifyError(message.getString());
    } else {
      notifyInformation(message.getString());
    }
  });
}

//
// REMARK: This method is executed in separate thread
//
void LocalFilesDeleteOperation::deleteFiles()
{
  for (UINT32 i = 0; i < m_filesToDelete.size(); i++) {
    // Create path to file from root folder and filename
    StringStorage pathToTargetFile(m_pathToTargetRoot);
    if (!pathToTargetFile.endsWith(_T('\\'))) {
      pathToTargetFile.appendString(_T("\\"));
    }
    pathToTargetFile.appendString(m_filesToDelete[i].getFileName());

    File file(pathToTargetFile.getString());

    //
    // Logging
    //

    StringStorage message;
    message.format(_T("Deleting local '%s' %s"), pathToTargetFile.getString(),
                   file.isDirectory() ? _T("folder") : _T("file"));

    report(message, false);

    // Delete file
    deleteFile(&file);
  } // for all files in file list

  // Notify listeners that operation have ended, after the reports above
  m_runner.postToNetwork([this] { notifyFinish(); });
}

bool LocalFilesDeleteOperation::deleteFile(File *file)
{
  if (m_isStopRequested) {
    return false;
  }

  // Full path to local file
  StringStorage pathToTargetFile;
  file->getPath(&pathToTargetFile);

  // Remove files in directory first. A link to a directory (a symbolic
  // link or a junction) is removed as it is: what it points to is not
  // part of what is deleted.
  if (file->isDirectory() && !file->isLink()) {
    std::vector<StringStorage> files;
    if (file->list(&files)) {
      for (const StringStorage &fileName : files) {
        if (m_isStopRequested) {
          return false;
        }
        // Create path to subfile
        StringStorage pathToTargetSubFile(pathToTargetFile.getString());
        pathToTargetSubFile.appendString(_T("\\"));
        pathToTargetSubFile.appendString(fileName.getString());

        File subFile(pathToTargetSubFile.getString());
        // Delete file
        deleteFile(&subFile);
      } // for all subfiles
    } else {
      StringStorage message;

      message.format(_T("Error: failed to get file list in local folder '%s'"),
                     pathToTargetFile.getString());

      report(message, true);
    }
  } // if file to delete is directory

  bool returnVal = file->remove();

  if (!returnVal) {
    StringStorage message;

    message.format(_T("Error: failed to remove local '%s' %s"),
                   pathToTargetFile.getString(),
                   file->isDirectory() ? _T("folder") : _T("file"));

    report(message, true);
  } // if failed to remove

  return returnVal;
}
