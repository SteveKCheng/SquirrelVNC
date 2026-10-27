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

#include "CopyOperation.h"

CopyOperation::CopyOperation(LogWriter *logWriter)
: FileTransferOperation(logWriter),
  m_copyListener(nullptr),
  m_totalBytesToCopy(0),
  m_totalBytesCopied(0),
  m_toCopy(nullptr),
  m_isAskingTargetFileExists(false)
{
}

CopyOperation::~CopyOperation()
{
}

void CopyOperation::setCopyProcessListener(CopyFileEventListener *listener)
{
  m_copyListener = listener;
}

void CopyOperation::answerTargetFileExists(int action)
{
  if (!m_isAskingTargetFileExists) {
    return;
  }
  m_isAskingTargetFileExists = false;
  onTargetFileExistsAnswer(action);
}

bool CopyOperation::askTargetFileExists(const FileInfo &sourceFileInfo,
                                        const FileInfo &targetFileInfo)
{
  m_existingTargetInfo = targetFileInfo;
  m_isAskingTargetFileExists = m_copyListener->targetFileExists(sourceFileInfo,
                                                                targetFileInfo,
                                                                m_pathToTargetFile.getString());
  return m_isAskingTargetFileExists;
}

bool CopyOperation::isAskingTargetFileExists() const
{
  return m_isAskingTargetFileExists;
}
