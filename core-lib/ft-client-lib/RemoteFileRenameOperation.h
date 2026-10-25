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

#ifndef _REMOTE_FILE_RENAME_OPERATION_H_
#define _REMOTE_FILE_RENAME_OPERATION_H_

#include "FileTransferOperation.h"

//
// Class used for renaming files on remote file system.
//

class RemoteFileRenameOperation final : public FileTransferOperation
{
public:
  RemoteFileRenameOperation(LogWriter *logWriter,
                            const TCHAR *pathToSourceFile,
                            const TCHAR *pathToTargetFile);

  RemoteFileRenameOperation(LogWriter *logWriter,
                            FileInfo sourceFileInfo,
                            FileInfo targetFileInfo,
                            const TCHAR *pathToTargetRoot);

  ~RemoteFileRenameOperation() override;

  void start() override;

protected:
  void onMvReply() override;
  void onLastRequestFailedReply() override;

protected:
  StringStorage m_pathToSourceFile;
  StringStorage m_pathToTargetFile;
};

#endif
