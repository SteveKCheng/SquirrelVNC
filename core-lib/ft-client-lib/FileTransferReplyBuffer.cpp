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

#include "FileTransferReplyBuffer.h"
#include "ft-common/FTMessage.h"
#include "io-lib/ByteArrayInputStream.h"
#include <crtdbg.h>

FileTransferReplyBuffer::FileTransferReplyBuffer(LogWriter *logWriter)
: m_logWriter(logWriter),
  m_isCompressionSupported(false),
  m_downloadBufferSize(0), 
  m_downloadFileFlags(0), m_downloadLastModified(0),
  m_dirSize(0)
{
  m_lastErrorMessage.setString(_T(""));
}

FileTransferReplyBuffer::~FileTransferReplyBuffer()
{
}

void FileTransferReplyBuffer::getLastErrorMessage(StringStorage *storage)
{
  *storage = m_lastErrorMessage;
}

bool FileTransferReplyBuffer::isCompressionSupported()
{
  return m_isCompressionSupported;
}

UINT32 FileTransferReplyBuffer::getFilesInfoCount()
{
  // The count read from the server is a UINT32.
  return static_cast<UINT32>(m_filesInfo.size());
}

FileInfo *FileTransferReplyBuffer::getFilesInfo()
{
  return m_filesInfo.data();
}

UINT32 FileTransferReplyBuffer::getDownloadBufferSize()
{
  return m_downloadBufferSize;
}

UINT64 FileTransferReplyBuffer::getDownloadLastModified()
{
  return m_downloadLastModified;
}

UINT64 FileTransferReplyBuffer::getDirSize()
{
  return m_dirSize;
}

std::vector<UINT8> FileTransferReplyBuffer::getDownloadBuffer()
{
  return m_downloadBuffer;
}

Task<> FileTransferReplyBuffer::readReply(AwaitableReader &reader, UINT32 rfbMessage)
{
  switch (rfbMessage) {
  case FTMessage::COMPRESSION_SUPPORT_REPLY:
    co_await readCompressionSupportReply(reader);
    break;
  case FTMessage::FILE_LIST_REPLY:
    co_await readFileListReply(reader);
    break;
  case FTMessage::DOWNLOAD_START_REPLY:
    readDownloadReply();
    break;
  case FTMessage::DOWNLOAD_DATA_REPLY:
    co_await readDownloadDataReply(reader);
    break;
  case FTMessage::DOWNLOAD_END_REPLY:
    co_await readDownloadEndReply(reader);
    break;
  case FTMessage::UPLOAD_START_REPLY:
    readUploadReply();
    break;
  case FTMessage::UPLOAD_DATA_REPLY:
    readUploadDataReply();
    break;
  case FTMessage::UPLOAD_END_REPLY:
    readUploadEndReply();
    break;
  case FTMessage::MD5_REPLY:
    readMd5DataReply();
    break;
  case FTMessage::DIRSIZE_REPLY:
    co_await readDirSizeReply(reader);
    break;
  case FTMessage::RENAME_REPLY:
    readMvReply();
    break;
  case FTMessage::MKDIR_REPLY:
    readMkdirReply();
    break;
  case FTMessage::REMOVE_REPLY:
    readRmReply();
    break;
  case FTMessage::LAST_REQUEST_FAILED_REPLY:
    co_await readLastRequestFailedReply(reader);
    break;
  }
}

Task<> FileTransferReplyBuffer::readCompressionSupportReply(AwaitableReader &reader)
{
  m_isCompressionSupported = (co_await reader.readUInt8() == 1);

  m_logWriter->info(_T("Received compression support reply: %s\n"),
                    m_isCompressionSupported ? _T("supported") : _T("not supported"));
}

Task<> FileTransferReplyBuffer::readFileListReply(AwaitableReader &reader)
{
  UINT8 compressionLevel = 0;
  UINT32 compressedSize = 0;
  UINT32 uncompressedSize = 0;

  std::vector<UINT8> buffer;

  {
    compressionLevel = co_await reader.readUInt8();
    compressedSize = co_await reader.readUInt32();
    uncompressedSize = co_await reader.readUInt32();

    buffer = co_await readCompressedDataBlock(reader,
                                              compressedSize,
                                              uncompressedSize,
                                              compressionLevel);
  }

  if (!buffer.empty()) {
    // FIXME: type conversion in C-style
    ByteArrayInputStream memoryInputStream(reinterpret_cast<char *>(&buffer.front()),
                                           buffer.size());
    DataInputStream filesInfoReader(&memoryInputStream);

    m_filesInfo.clear();

    UINT32 filesInfoCount = filesInfoReader.readUInt32();
    // Each entry takes at least 22 bytes (size, time, flags and the length
    // of its name), so a count that the data cannot hold is rejected before
    // allocating for it.
    const size_t MIN_ENTRY_SIZE = 8 + 8 + 2 + 4;
    if (filesInfoCount > (buffer.size() - 4) / MIN_ENTRY_SIZE) {
      throw IOException(_T("File list is shorter than its count of files"));
    }
    m_filesInfo.resize(filesInfoCount);

    for (UINT32 i = 0; i < filesInfoCount; i++) {
      FileInfo *fileInfo = &m_filesInfo[i];

      fileInfo->setSize(filesInfoReader.readUInt64());
      fileInfo->setLastModified(filesInfoReader.readUInt64());
      fileInfo->setFlags(filesInfoReader.readUInt16());

      StringStorage t;
      filesInfoReader.readUTF8(&t);

      fileInfo->setFileName(t.getString());
    } // for all newly created file's info

    m_logWriter->info(_T("Received file list reply: \n")
                      _T("\t files count = %u\n")
                      _T("\t use compression = %d\n"),
                      filesInfoCount, compressionLevel);
  } else {
    m_logWriter->info(_T("Received file list reply is not read: ")
                      _T("compressed buffer is empty"));
  }
}

void FileTransferReplyBuffer::readMd5DataReply()
{
  throw OperationNotSupportedException();
}

void FileTransferReplyBuffer::readUploadReply()
{
  m_logWriter->info(_T("Received upload reply\n"));
}

void FileTransferReplyBuffer::readUploadDataReply()
{
  m_logWriter->info(_T("Received upload data reply\n"));
}

void FileTransferReplyBuffer::readUploadEndReply()
{
  m_logWriter->info(_T("Received upload end reply\n"));
}

void FileTransferReplyBuffer::readDownloadReply()
{
  m_logWriter->info(_T("Received download reply\n"));
}

Task<> FileTransferReplyBuffer::readDownloadDataReply(AwaitableReader &reader)
{
  UINT8 coLevel = co_await reader.readUInt8();
  UINT32 coBufferSize = co_await reader.readUInt32();
  UINT32 uncoBufferSize = co_await reader.readUInt32();

  m_downloadBuffer = co_await readCompressedDataBlock(reader, coBufferSize, uncoBufferSize, coLevel);
  // readCompressedDataBlock() guarantees this is uncoBufferSize, but the
  // buffer's own size is what bounds the reads made with it.
  m_downloadBufferSize = (UINT32)m_downloadBuffer.size();

  m_logWriter->info(_T("Received download data reply:\n")
                    _T("\tcompressed size: %u\n")
                    _T("\tuncompressed size: %u\n")
                    _T("\tuse compression: %d\n"),
                    coBufferSize, uncoBufferSize, coLevel);
}

Task<> FileTransferReplyBuffer::readDownloadEndReply(AwaitableReader &reader)
{
  m_downloadFileFlags = co_await reader.readUInt8();
  m_downloadLastModified = co_await reader.readUInt64();

  m_logWriter->info(_T("Received download end reply:\n")
                    _T("\tfile flags: %d\n")
                    _T("\tmodification time: %llu\n"),
                    m_downloadFileFlags, m_downloadLastModified);
}

void FileTransferReplyBuffer::readMkdirReply()
{
  m_logWriter->info(_T("Received mkdir reply\n"));
}

void FileTransferReplyBuffer::readRmReply()
{
  m_logWriter->info(_T("Received rm reply\n"));
}

void FileTransferReplyBuffer::readMvReply()
{
  m_logWriter->info(_T("Received rename reply\n"));
}

Task<> FileTransferReplyBuffer::readDirSizeReply(AwaitableReader &reader)
{
  m_dirSize = co_await reader.readUInt64();

  m_logWriter->info(_T("Received dirsize reply\n"));
}

Task<> FileTransferReplyBuffer::readLastRequestFailedReply(AwaitableReader &reader)
{
  co_await reader.readUTF8(&m_lastErrorMessage);

  m_logWriter->info(_T("Received last request failed reply:\n")
                    _T("\terror message: %s\n"),
                    m_lastErrorMessage.getString());
}

Task<std::vector<UINT8>> FileTransferReplyBuffer::readCompressedDataBlock(AwaitableReader &reader,
                                                               UINT32 compressedSize,
                                                               UINT32 uncompressedSize,
                                                               UINT8 compressionLevel)
{
  //
  // Buffers with compressed and uncompressed data.
  // When not using compression uncoBuffer = coBuffer.
  //

  UINT32 coSize = compressedSize;
  UINT32 uncoSize = uncompressedSize;

  if (coSize > MAX_DATA_BLOCK_SIZE || uncoSize > MAX_DATA_BLOCK_SIZE) {
    throw IOException(_T("Data block is too large"));
  }

  // Callers use the size of the result as the size of the data, so it must
  // not differ from what the server says it is.
  if (compressionLevel == 0 && coSize != uncoSize) {
    throw IOException(_T("Sizes of an uncompressed data block differ"));
  }

  std::vector<UINT8> coBuffer(coSize);

  //
  // Read compressed data
  //

  if (coSize == 0) {
    if (uncoSize != 0) {
      throw IOException(_T("Compressed data block is empty"));
    }
    co_return coBuffer;
  }

  co_await reader.readFully(&coBuffer.front(), coSize);

  if (compressionLevel == 0) {
    co_return coBuffer;
  }

  std::vector<UINT8> uncoBuffer(uncoSize);

  m_inflater.setUnpackedSize(uncoSize);
  // FIXME: type conversion in C-style
  m_inflater.setInput((const char*)&coBuffer.front(), coSize);

  m_inflater.inflate();

  // The inflater's output buffer holds at least uncoSize bytes, but only
  // getOutputSize() of them are data.
  if (m_inflater.getOutputSize() != uncoSize) {
    throw IOException(_T("Data block does not inflate to its stated size"));
  }

  if (uncoSize != 0) {
    memcpy(&uncoBuffer.front(), m_inflater.getOutput(), uncoSize);
  }

  co_return uncoBuffer;
}
