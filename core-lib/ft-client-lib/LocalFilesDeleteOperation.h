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

#ifndef _LOCAL_FILES_DELETE_OPERATION_H_
#define _LOCAL_FILES_DELETE_OPERATION_H_

#include "FileTransferOperation.h"
#include "thread/Thread.h"

class LocalFilesDeleteOperation : public FileTransferOperation,
                                  private Thread
{
public:
  // Pre-conditions: filesToDelete points to filesCount entries, which are
  // copied.
  LocalFilesDeleteOperation(LogWriter *logWriter,
                            const FileInfo *filesToDelete,
                            size_t filesCount,
                            const TCHAR *pathToTargetRoot);
  virtual ~LocalFilesDeleteOperation();

  virtual void start();
  virtual void terminate();

private:

  virtual void execute();

  //
  // Deletes file, and first the files in it if it is a directory; a
  // directory that cannot be listed is reported with notifyError().
  //
  // Post-conditions: returns true if file was removed; false, after
  // notifyError(), if it could not be, or if the operation is terminating.
  //
  bool deleteFile(File *file);

protected:
  StringStorage m_pathToTargetRoot;
  std::vector<FileInfo> m_filesToDelete;
};

#endif
