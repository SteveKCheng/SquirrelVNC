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
#include "BlockingWorkRunner.h"

#include "file-lib/File.h"

#include <atomic>

//
// Deletes local files. Like every operation it is driven on the network
// thread, but the deletion, which may take long, runs on the thread for
// blocking work (BlockingWorkRunner), and reports its messages and its end
// back on the network thread.
//
class LocalFilesDeleteOperation final : public FileTransferOperation
{
public:
  // Pre-conditions: filesToDelete points to filesCount entries, which are
  // copied; runner outlives this operation.
  LocalFilesDeleteOperation(LogWriter *logWriter,
                            BlockingWorkRunner &runner,
                            const FileInfo *filesToDelete,
                            size_t filesCount,
                            const TCHAR *pathToTargetRoot);
  ~LocalFilesDeleteOperation() override;

  //
  // Reports the start, and runs the deletion (deleteFiles()) on the thread
  // for blocking work; its end is reported on the network thread. If it
  // cannot run, reports an error and the end at once.
  //
  // Pre-conditions: network thread; called once.
  //
  void start() override;

  //
  // Asks the deletion to stop, before the next file; it still reports its
  // end. Network thread.
  //
  void terminate() override;

private:
  //
  // The deletion, on the thread for blocking work: deletes each file, then
  // posts the end of the operation to the network thread.
  //
  void deleteFiles();

  //
  // Posts message to the network thread, to report it there, as an error
  // or as information. Any thread.
  //
  void report(const StringStorage &message, bool isError);

  //
  // Deletes file, and first the files in it if it is a directory; a
  // directory that cannot be listed is reported as an error. A symbolic
  // link or junction is deleted itself, without what it points to.
  //
  // Post-conditions: returns true if file was removed; false, after
  // reporting an error, if it could not be, or if the operation is
  // terminating. On the thread for blocking work.
  //
  bool deleteFile(File *file);

protected:
  BlockingWorkRunner &m_runner;
  StringStorage m_pathToTargetRoot;
  std::vector<FileInfo> m_filesToDelete;
  // Set by terminate(), read by the deletion on the other thread.
  std::atomic<bool> m_isStopRequested;
};

#endif
