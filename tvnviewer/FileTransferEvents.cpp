// Copyright (C) 2026 Steve Cheng.
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

#include "FileTransferEvents.h"

#include "FileTransferMainDialog.h"

#include "ft-client-lib/CopyFileEventListener.h"

#include <utility>

FileTransferEvents::FileTransferEvents()
: m_dialog(nullptr),
  m_window(nullptr)
{
}

FileTransferEvents::~FileTransferEvents()
{
}

FileTransferInterface &FileTransferEvents::getInterface()
{
  return *this;
}

void FileTransferEvents::attach(FileTransferMainDialog &dialog, HWND window)
{
  std::lock_guard<std::mutex> lock(m_lock);
  m_dialog = &dialog;
  m_window = window;
  m_pendingProgress.reset();
  m_pendingMessages.clear();
}

void FileTransferEvents::detach() noexcept
{
  std::lock_guard<std::mutex> lock(m_lock);
  m_dialog = nullptr;
  m_window = nullptr;
}

std::optional<double> FileTransferEvents::takeProgress()
{
  std::lock_guard<std::mutex> lock(m_lock);
  return std::exchange(m_pendingProgress, std::nullopt);
}

std::vector<StringStorage> FileTransferEvents::takeMessages()
{
  std::lock_guard<std::mutex> lock(m_lock);
  return std::exchange(m_pendingMessages, {});
}

bool FileTransferEvents::post(UINT message, WPARAM wParam, LPARAM lParam) noexcept
{
  return m_window != nullptr && PostMessage(m_window, message, wParam, lParam) != 0;
}

void FileTransferEvents::setProgress(double progress)
{
  std::lock_guard<std::mutex> lock(m_lock);
  bool isWaiting = m_pendingProgress.has_value();
  m_pendingProgress = progress;
  // If it cannot be posted, nothing is waiting, and a later call tries again.
  if (!isWaiting && !post(WM_FT_PROGRESS)) {
    m_pendingProgress.reset();
  }
}

void FileTransferEvents::onFtOpError(const TCHAR *message)
{
  std::lock_guard<std::mutex> lock(m_lock);
  m_pendingMessages.emplace_back(message);
  if (m_pendingMessages.size() == 1 && !post(WM_FT_MESSAGES)) {
    m_pendingMessages.clear();
  }
}

void FileTransferEvents::onFtOpInfo(const TCHAR *message)
{
  // The dialog shows both kinds alike, in one list.
  onFtOpError(message);
}

void FileTransferEvents::onFtOpStarted()
{
  std::lock_guard<std::mutex> lock(m_lock);
  post(WM_FT_STARTED);
}

void FileTransferEvents::onFtOpFinished(int state, int result)
{
  std::lock_guard<std::mutex> lock(m_lock);
  post(WM_FT_FINISHED, static_cast<WPARAM>(state), static_cast<LPARAM>(result));
}

int FileTransferEvents::onFtTargetFileExists(FileInfo *sourceFileInfo,
                                             FileInfo *targetFileInfo,
                                             const TCHAR *pathToTargetFile)
{
  FileTransferMainDialog *dialog;
  {
    std::lock_guard<std::mutex> lock(m_lock);
    dialog = m_dialog;
  }
  if (dialog == nullptr) {
    return CopyFileEventListener::TFE_CANCEL;
  }
  // As before this class: the dialog is shown from this thread, and is not
  // kept alive while it is open.
  return dialog->onFtTargetFileExists(sourceFileInfo, targetFileInfo,
                                      pathToTargetFile);
}

void FileTransferEvents::setNothingState()
{
  if (m_dialog != nullptr) {
    m_dialog->setNothingState();
  }
}

void FileTransferEvents::onRefreshLocalFileList()
{
  if (m_dialog != nullptr) {
    m_dialog->onRefreshLocalFileList();
  }
}

void FileTransferEvents::onRefreshRemoteFileList()
{
  if (m_dialog != nullptr) {
    m_dialog->onRefreshRemoteFileList();
  }
}

void FileTransferEvents::raise(const Exception &ex)
{
  if (m_dialog != nullptr) {
    m_dialog->raise(ex);
  }
  // The exception being handled, which is ex.
  throw;
}
