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


#include "FileTransferCapability.h"

#include "ft-client-lib/FileTransferRequestSender.h"
#include "ft-client-lib/FileTransferReplyBuffer.h"
#include "ft-client-lib/FileTransferMessageProcessor.h"
#include "ft-client-lib/FileTransferCore.h"

#include "ft-common/FTMessage.h"
#include "rfb/VendorDefs.h"

#include <asio/post.hpp>

#include <utility>

FileTransferCapability::FileTransferCapability(Logger *logger)
: m_logWriter(logger),
  m_ftRequestSender(&m_logWriter),
  m_ftReplyBuffer(&m_logWriter),
  m_ftMessageProcessor(m_ftReplyBuffer),
  m_ftCore(&m_logWriter,
           &m_ftRequestSender,
           &m_ftReplyBuffer,
           &m_ftMessageProcessor,
           *this),
  m_core(nullptr),
  m_blockingWorkPool(nullptr)
{
}

FileTransferCapability::~FileTransferCapability()
{
}

void FileTransferCapability::setOutput(DataOutputStream *output)
{
  m_ftRequestSender.setOutput(output);
}

void FileTransferCapability::setInterface(FileTransferInterface *ftInterface)
{
  m_ftCore.setInterface(ftInterface);
}

Task<> FileTransferCapability::onServerMessage(UINT32 code, AwaitableReader &reader)
{
  co_await m_ftMessageProcessor.processRfbMessage(reader, code);
}

void FileTransferCapability::post(std::function<void(FileTransferCore &)> command)
{
  m_core->postToLoop([this, command = std::move(command)] {
    try {
      command(m_ftCore);
    } catch (const Exception &ex) {
      m_logWriter.error(_T("Error in a file-transfer command: %s"), ex.getMessage());
    }
  });
}

void FileTransferCapability::onConnectionEnded()
{
  m_ftCore.onConnectionEnded();
}

void FileTransferCapability::setBlockingWorkPool(BlockingWorkPool &pool)
{
  m_blockingWorkPool = &pool;
}

bool FileTransferCapability::runBlocking(std::function<void()> work)
{
  std::shared_ptr<void> owner = m_core->claimOwner();
  if (!owner || m_blockingWorkPool == nullptr) {
    return false;
  }
  asio::post(m_blockingWorkPool->getExecutor(),
             [this, owner = std::move(owner), work = std::move(work)] {
    try {
      work();
    } catch (const Exception &ex) {
      m_logWriter.error(_T("Error in blocking file-transfer work: %s"), ex.getMessage());
    }
  });
  return true;
}

void FileTransferCapability::postToNetwork(std::function<void()> work)
{
  m_core->postToLoop(std::move(work));
}

void FileTransferCapability::addCapabilities(CapabilitiesManager &core)
{
  m_core = &core;
  CapabilitiesManager *capabilities = &core;

  // Client-to-Server messages:
  capabilities->addClientMsgCapability(FTMessage::COMPRESSION_SUPPORT_REQUEST,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::COMPRESSION_SUPPORT_REQUEST_SIG,
                                  _T("Compression support"));

  capabilities->addClientMsgCapability(FTMessage::FILE_LIST_REQUEST,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::FILE_LIST_REQUEST_SIG,
                                  _T("File list request"));

  capabilities->addClientMsgCapability(FTMessage::MD5_REQUEST,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::MD5_REQUEST_SIG,
                                  _T("File md5 sum request"));

  capabilities->addClientMsgCapability(FTMessage::DIRSIZE_REQUEST,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::DIRSIZE_REQUEST_SIG,
                                  _T("Directory size request"));

  capabilities->addClientMsgCapability(FTMessage::RENAME_REQUEST,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::RENAME_REQUEST_SIG,
                                  _T("File move request"));

  capabilities->addClientMsgCapability(FTMessage::MKDIR_REQUEST,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::MKDIR_REQUEST_SIG,
                                  _T("Directory create request"));

  capabilities->addClientMsgCapability(FTMessage::REMOVE_REQUEST,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::REMOVE_REQUEST_SIG,
                                  _T("File remove request"));

  capabilities->addClientMsgCapability(FTMessage::DOWNLOAD_START_REQUEST,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::DOWNLOAD_START_REQUEST_SIG,
                                  _T("File download start request"));

  capabilities->addClientMsgCapability(FTMessage::DOWNLOAD_DATA_REQUEST,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::DOWNLOAD_DATA_REQUEST_SIG,
                                  _T("File download data request"));

  capabilities->addClientMsgCapability(FTMessage::UPLOAD_START_REQUEST,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::UPLOAD_START_REQUEST_SIG,
                                  _T("File upload start request"));

  capabilities->addClientMsgCapability(FTMessage::UPLOAD_DATA_REQUEST,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::UPLOAD_DATA_REQUEST_SIG,
                                  _T("File upload data request"));

  capabilities->addClientMsgCapability(FTMessage::UPLOAD_END_REQUEST,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::UPLOAD_END_REQUEST_SIG,
                                  _T("File upload end request"));

  // Server-to-Client messages:
  capabilities->addServerMsgCapability(this,
                                  FTMessage::COMPRESSION_SUPPORT_REPLY,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::COMPRESSION_SUPPORT_REPLY_SIG,
                                  _T("Compression support"));

  capabilities->addServerMsgCapability(this,
                                  FTMessage::FILE_LIST_REPLY,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::FILE_LIST_REPLY_SIG,
                                  _T("File list reply"));

  capabilities->addServerMsgCapability(this,
                                  FTMessage::LAST_REQUEST_FAILED_REPLY,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::LAST_REQUEST_FAILED_REPLY_SIG,
                                  _T("Last request failed"));

  capabilities->addServerMsgCapability(this,
                                  FTMessage::MD5_REPLY,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::MD5_REPLY_SIG,
                                  _T("File md5 sum reply"));

  capabilities->addServerMsgCapability(this,
                                  FTMessage::DIRSIZE_REPLY,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::DIRSIZE_REPLY_SIG,
                                  _T("Directory size reply"));

  capabilities->addServerMsgCapability(this,
                                  FTMessage::RENAME_REPLY,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::RENAME_REPLY_SIG,
                                  _T("File move reply"));

  capabilities->addServerMsgCapability(this,
                                  FTMessage::MKDIR_REPLY,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::MKDIR_REPLY_SIG,
                                  _T("Directory create reply"));

  capabilities->addServerMsgCapability(this,
                                  FTMessage::REMOVE_REPLY,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::REMOVE_REPLY_SIG,
                                 _T("File remove reply"));

  capabilities->addServerMsgCapability(this,
                                  FTMessage::DOWNLOAD_START_REPLY,
                                 VendorDefs::TIGHTVNC,
                                 FTMessage::DOWNLOAD_START_REPLY_SIG,
                                 _T("File download start reply"));

  capabilities->addServerMsgCapability(this,
                                  FTMessage::DOWNLOAD_DATA_REPLY,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::DOWNLOAD_DATA_REPLY_SIG,
                                  _T("File download data reply"));

  capabilities->addServerMsgCapability(this,
                                  FTMessage::DOWNLOAD_END_REPLY,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::DOWNLOAD_END_REPLY_SIG,
                                  _T("File download end reply"));

  capabilities->addServerMsgCapability(this,
                                  FTMessage::UPLOAD_START_REPLY,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::UPLOAD_START_REPLY_SIG,
                                  _T("File upload start reply"));

  capabilities->addServerMsgCapability(this,
                                  FTMessage::UPLOAD_DATA_REPLY,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::UPLOAD_DATA_REPLY_SIG,
                                  _T("File upload data reply"));

  capabilities->addServerMsgCapability(this,
                                  FTMessage::UPLOAD_END_REPLY,
                                  VendorDefs::TIGHTVNC,
                                  FTMessage::UPLOAD_END_REPLY_SIG,
                                  _T("File upload end reply"));
}
