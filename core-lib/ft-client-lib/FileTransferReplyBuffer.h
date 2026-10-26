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

#ifndef _FILE_TRANSFER_REPLY_BUFFER_H_
#define _FILE_TRANSFER_REPLY_BUFFER_H_

#include "io-lib/AwaitableReader.h"
#include "io-lib/DataInputStream.h"
#include "util/Task.h"

#include "ft-common/FileInfo.h"
#include "util/Inflater.h"
#include "util/ZLibException.h"

#include "ft-common/OperationNotSupportedException.h"

#include "log-writer/LogWriter.h"

#include <vector>

class FileTransferReplyBuffer final
{
public:
  FileTransferReplyBuffer(LogWriter *logWriter);
  ~FileTransferReplyBuffer();

  void getLastErrorMessage(StringStorage *storage);

  bool isCompressionSupported();

  // The list of the last file list reply: getFilesInfo() points to
  // getFilesInfoCount() entries, valid until the next reply.
  UINT32 getFilesInfoCount();
  FileInfo *getFilesInfo();

  UINT32 getDownloadBufferSize();
  std::vector<UINT8> getDownloadBuffer();

  UINT64 getDownloadLastModified();

  UINT64 getDirSize();

  //
  // Reads one file-transfer reply from reader, and keeps what it holds for
  // the getters above (and the operations, through them). Called on the
  // core thread, before the listeners of FileTransferMessageProcessor are
  // notified of the reply. Does nothing for other message types.
  //
  // A coroutine (util/Task.h): the read*Reply() functions that read the
  // stream are coroutines too, and await their reads; those that read
  // nothing (the reply is the message type alone) only log.
  //
  // Pre-conditions: the message type, rfbMessage, has been read from the
  // stream.
  // Post-conditions: the reply has been read from the stream.
  // Throws whatever the read*Reply() function for the type throws; then
  // the stream is left in the middle of the reply.
  //
  Task<> readReply(AwaitableReader &reader, UINT32 rfbMessage);

private:
  Task<> readCompressionSupportReply(AwaitableReader &reader);
  // Reads a file list reply into m_filesInfo, replacing the previous list.
  // If this throws, the previous list is gone and the new one may be partly
  // read: some entries may be left empty.  The count the server sends is
  // rejected if the data is too short to hold that many entries.
  Task<> readFileListReply(AwaitableReader &reader);
  void readMd5DataReply();

  void readUploadReply();
  void readUploadDataReply();
  void readUploadEndReply();

  void readDownloadReply();
  Task<> readDownloadDataReply(AwaitableReader &reader);
  Task<> readDownloadEndReply(AwaitableReader &reader);

  void readMkdirReply();
  void readRmReply();
  void readMvReply();

  Task<> readDirSizeReply(AwaitableReader &reader);
  Task<> readLastRequestFailedReply(AwaitableReader &reader);

  /**
   * Reads a block of compressedSize bytes from reader, and inflates it with
   * m_inflater unless compressionLevel is 0.
   *
   * The sizes come from the server.  The result always holds exactly
   * uncompressedSize bytes, so callers may use its size as the size of the
   * data.
   *
   * @throws IOException if either size is over MAX_DATA_BLOCK_SIZE, leaving
   *   the block unread; if the sizes are inconsistent: when the block is
   *   not compressed and the sizes differ, when it is empty but the
   *   uncompressed size is not 0, or when it does not inflate to exactly
   *   uncompressedSize bytes; or on errors reading input.
   * @throws ZLibException if the data cannot be inflated.
   */
  // Largest compressed or uncompressed size of a data block that
  // readCompressedDataBlock() accepts, so that the server cannot make the
  // viewer allocate up to 4 GiB twice over.  Download blocks are at most the
  // 400,000 bytes the viewer requests; file lists of very large folders
  // are the largest blocks expected.
  static const UINT32 MAX_DATA_BLOCK_SIZE = 64 * 1024 * 1024;

  Task<std::vector<UINT8>> readCompressedDataBlock(AwaitableReader &reader,
                                                   UINT32 compressedSize,
                                                   UINT32 uncompressedSize,
                                                   UINT8 compressionLevel);

protected:

  //
  // Interface of log writer for log data
  //
  LogWriter *m_logWriter;

  //
  // ZLib stream for decompression of compressed data
  //
  Inflater m_inflater;

  //
  // Members than have access from public methods
  //

  // Compression support reply
  bool m_isCompressionSupported;

  // File list reply
  std::vector<FileInfo> m_filesInfo;

  // Last request message failed reply
  StringStorage m_lastErrorMessage;

  // Download data reply
  std::vector<UINT8> m_downloadBuffer;
  UINT32 m_downloadBufferSize;

  // Download end reply
  UINT8 m_downloadFileFlags;
  UINT64 m_downloadLastModified;

  // Dirsize reply data
  UINT64 m_dirSize;
};

#endif
