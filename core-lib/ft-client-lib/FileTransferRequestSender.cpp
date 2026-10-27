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

#include "FileTransferRequestSender.h"
#include "ft-common/FTMessage.h"

FileTransferRequestSender::FileTransferRequestSender(LogWriter *logWriter)
: m_logWriter(logWriter),
  m_output(nullptr)
{
}

FileTransferRequestSender::~FileTransferRequestSender()
{
}

void FileTransferRequestSender::setOutput(RfbOutputGate *outputStream)
{
  m_output = outputStream;
}

void FileTransferRequestSender::sendFileListRequest(const TCHAR *fullPath,
                                                    bool useCompression)
{
  UINT32 messageId = FTMessage::FILE_LIST_REQUEST;
  UINT8 compressionLevel = useCompression ? (UINT8)1 : (UINT8)0;

  m_logWriter->info(_T("Sending file list request with parameters:\n")
                    _T("\tpath = %s\n")
                    _T("\tuse compression = %d\n"),
                    fullPath,
                    useCompression ? 1 : 0);

  m_output->writeUInt32(messageId);
  m_output->writeUInt8(compressionLevel);
  m_output->writeUTF8(fullPath);
  m_output->flush();
}

void FileTransferRequestSender::sendDownloadRequest(const TCHAR *fullPathName,
                                                    UINT64 offset)
{
  m_logWriter->info(_T("Sending download request with parameters:\n")
                    _T("\tpath = %s\n")
                    _T("\toffset = %llu\n"),
                    fullPathName, offset);

  m_output->writeUInt32(FTMessage::DOWNLOAD_START_REQUEST);
  m_output->writeUTF8(fullPathName);
  m_output->writeUInt64(offset);
  m_output->flush();
}

void FileTransferRequestSender::sendDownloadDataRequest(UINT32 size,
                                                        bool useCompression)
{
  UINT8 compressionLevel = useCompression ? (UINT8)1 : (UINT8)0;

  m_logWriter->info(_T("Sending download data request with parameters:\n")
                    _T("\tsize = %u\n")
                    _T("\tuse compression = %d\n"),
                    size,
                    compressionLevel);

  m_output->writeUInt32(FTMessage::DOWNLOAD_DATA_REQUEST);
  m_output->writeUInt8(compressionLevel);
  m_output->writeUInt32(size);
  m_output->flush();
}

void FileTransferRequestSender::sendRmFileRequest(const TCHAR *fullPathName)
{
  m_logWriter->info(_T("Sending rm file request with parameters:\n\tpath = %s\n"),
                    fullPathName);

  m_output->writeUInt32(FTMessage::REMOVE_REQUEST);
  m_output->writeUTF8(fullPathName);
  m_output->flush();
}

void FileTransferRequestSender::sendMkDirRequest(const TCHAR *fullPathName)
{
  m_logWriter->info(_T("Sending mkdir request with parameters:\n\tpath = %s\n"),
                    fullPathName);

  m_output->writeUInt32(FTMessage::MKDIR_REQUEST);
  m_output->writeUTF8(fullPathName);
  m_output->flush();
}

void FileTransferRequestSender::sendMvFileRequest(const TCHAR *oldFileName,
                                                  const TCHAR *newFileName)
{
  m_logWriter->info(_T("Sending rename file request with parameters:\n")
                    _T("\t old path = %s\n")
                    _T("\t new path = %s\n"),
                    oldFileName,
                    newFileName);

  m_output->writeUInt32(FTMessage::RENAME_REQUEST);
  m_output->writeUTF8(oldFileName);
  m_output->writeUTF8(newFileName);
  m_output->flush();
}

void FileTransferRequestSender::sendUploadRequest(const TCHAR *fullPathName,
                                                  bool overwrite,
                                                  UINT64 offset)
{
  UINT8 flags = 0;
  if (overwrite) {
    flags = 0x1;
  }

  m_logWriter->info(_T("Sending upload request with parameters:\n")
                    _T("\tpath = %s\n")
                    _T("\toverwrite flag = %d\n")
                    _T("\toffset = %llu\n"),
                    fullPathName,
                    overwrite ? 1 : 0,
                    offset);

  m_output->writeUInt32(FTMessage::UPLOAD_START_REQUEST);
  m_output->writeUTF8(fullPathName);
  m_output->writeUInt8(flags);
  m_output->writeUInt64(offset);
  m_output->flush();
}

void FileTransferRequestSender::sendUploadDataRequest(const char *buffer,
                                                      UINT32 size,
                                                      bool useCompression)
{
  m_output->writeUInt32(FTMessage::UPLOAD_DATA_REQUEST);

  //
  // FIXME: Compression is not supported for now
  // TODO: Compression support on upload
  //

  if (useCompression) {
    throw IOException(_T("Compression is not supported yet."));
  }

  UINT8 compressionLevel = useCompression ? (short)1 : (short)0;

  m_logWriter->info(_T("Sending upload data request with parameters:\n")
                    _T("\tsize = %u\n")
                    _T("\tuse compression = %d\n"),
                    size,
                    compressionLevel);

  m_output->writeUInt8(compressionLevel);
  m_output->writeUInt32(size);
  m_output->writeUInt32(size);
  m_output->writeFully(buffer, size);
  m_output->flush();
}

void FileTransferRequestSender::sendUploadEndRequest(UINT8 fileFlags,
                                                     UINT64 modificationTime)
{
  m_logWriter->info(_T("Sending upload end request with parameters:\n")
                    _T("\tflags = %d\n")
                    _T("\tmodification time = %llu\n"),
                    fileFlags,
                    modificationTime);

  m_output->writeUInt32(FTMessage::UPLOAD_END_REQUEST);
  m_output->writeUInt16(fileFlags);
  m_output->writeUInt64(modificationTime);
  m_output->flush();
}

void FileTransferRequestSender::sendFolderSizeRequest(const TCHAR *fullPath)
{
  m_logWriter->info(_T("Sending get folder size request with parameters:\n\tpath = %s\n"),
                    fullPath);

  m_output->writeUInt32(FTMessage::DIRSIZE_REQUEST);
  m_output->writeUTF8(fullPath);
  m_output->flush();
}
