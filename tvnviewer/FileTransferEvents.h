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

#ifndef _FILE_TRANSFER_EVENTS_H_
#define _FILE_TRANSFER_EVENTS_H_

#include "ft-client-lib/FileTransferInterface.h"

#include "util/StringStorage.h"

#include <mutex>
#include <optional>
#include <vector>

//
// The part of the file-transfer dialog that the file transfer calls: its
// FileTransferInterface. The operations run on the network thread, so the
// events do not touch the dialog: each keeps what it is given and posts a message to the dialog's
// window, whose handler (FileTransferMainDialog) takes it from here on the
// GUI thread. This is the pattern of ViewerCoreEvents.
//
// The interface is inherited privately, and its events are private: only
// the file transfer calls them, through getInterface().
//
// Lifetime: a member of ViewerWindow, given to the file transfer once and
// never replaced, so it outlives every call from the operations; the dialog
// comes and goes, and attaches itself while its window exists.
//
class FileTransferEvents final : private FileTransferInterface
{
public:
  //
  // The messages posted to the dialog, handled by FileTransferMainDialog.
  // WM_FT_FINISHED has the state of FileTransferCore in WPARAM and the
  // result in LPARAM, as given to onFtOpFinished().
  //
  static const UINT WM_FT_STARTED = WM_USER + 1;
  static const UINT WM_FT_FINISHED = WM_USER + 2;
  static const UINT WM_FT_PROGRESS = WM_USER + 3;
  static const UINT WM_FT_MESSAGES = WM_USER + 4;
  static const UINT WM_FT_CONNECTION_ENDED = WM_USER + 5;
  static const UINT WM_FT_TARGET_FILE_EXISTS = WM_USER + 6;

  //
  // The question of onFtTargetFileExists(), for WM_FT_TARGET_FILE_EXISTS:
  // what to do with target, which exists already, when copying source to
  // path.
  //
  struct TargetFileExistsQuestion
  {
    FileInfo source;
    FileInfo target;
    StringStorage path;
  };

  //
  // Nothing is posted until attach().
  //
  FileTransferEvents();
  FileTransferEvents(const FileTransferEvents &) = delete;
  FileTransferEvents &operator=(const FileTransferEvents &) = delete;
  FileTransferEvents(FileTransferEvents &&) = delete;
  FileTransferEvents &operator=(FileTransferEvents &&) = delete;
  ~FileTransferEvents() override;

  //
  // This object as the file transfer's interface
  // (FileTransferCapability::setInterface()).
  //
  // Post-conditions: the result refers to this object.
  //
  FileTransferInterface &getInterface();

  //
  // The rest of the public functions are for FileTransferMainDialog, and
  // only it calls them, on the GUI thread.
  //

  //
  // For the dialog, when its window is created (WM_INITDIALOG), before it
  // starts any operation.
  //
  // Pre-conditions: no dialog is attached; window is the dialog's window.
  // Post-conditions: the events are posted to window, and what earlier
  // events left untaken is dropped.
  //
  void attach(HWND window);

  //
  // For the dialog, when its window is being destroyed (WM_DESTROY):
  // nothing is posted any more, as the handle may be reused by another
  // window.
  //
  // Post-conditions: no dialog is attached.
  //
  void detach() noexcept;

  //
  // For the dialog's handlers: what the operations reported since the last
  // call. takeProgress() gives the latest progress, in [0.0, 1.0], if any
  // has come; takeMessages() the errors and information in the order they
  // came; takeFileList() the remote folder of the last successful file
  // list, for WM_FT_FINISHED; takeTargetFileExistsQuestion() the pending
  // question, for WM_FT_TARGET_FILE_EXISTS. They leave nothing behind.
  //
  std::optional<double> takeProgress();
  std::vector<StringStorage> takeMessages();
  std::optional<std::vector<FileInfo>> takeFileList();
  std::optional<TargetFileExistsQuestion> takeTargetFileExistsQuestion();

private:
  //
  // The events of FileTransferInterface.
  //
  // From the operations, on the network thread: setProgress(), onFtOpError() and
  // onFtOpInfo() keep what they are given, and post a message only if none
  // is waiting already, so that a flood of progress becomes one message;
  // onFtOpStarted(), onFtOpFinished() and onFtConnectionEnded() post their
  // message; onFtFileList() keeps the list, for the WM_FT_FINISHED that
  // follows.
  //
  void setProgress(double progress) override;
  void onFtOpError(const TCHAR *message) override;
  void onFtOpInfo(const TCHAR *message) override;
  void onFtOpStarted() override;
  void onFtOpFinished(int state, int result) override;
  void onFtFileList(std::vector<FileInfo> files) override;
  void onFtConnectionEnded() override;

  //
  // Keeps the question, and posts WM_FT_TARGET_FILE_EXISTS; returns
  // whether it was posted. The dialog posts the answer to the network
  // thread.
  //
  bool onFtTargetFileExists(const FileInfo &sourceFileInfo,
                            const FileInfo &targetFileInfo,
                            const TCHAR *pathToTargetFile) override;

  //
  // Posts message to the dialog's window, unless none is attached. Returns
  // whether it was posted. The caller holds m_lock, so that detach() cannot
  // come between the check and the post.
  //
  bool post(UINT message, WPARAM wParam = 0, LPARAM lParam = 0) noexcept;

  // Guards everything below.
  std::mutex m_lock;
  // The dialog's window; null before attach() and after detach().
  HWND m_window;
  // The latest progress not yet taken; while it is set, a WM_FT_PROGRESS is
  // waiting.
  std::optional<double> m_pendingProgress;
  // The messages not yet taken; while there are any, a WM_FT_MESSAGES is
  // waiting.
  std::vector<StringStorage> m_pendingMessages;
  // The list of onFtFileList(), until the dialog takes it.
  std::optional<std::vector<FileInfo>> m_pendingFileList;
  // The question of onFtTargetFileExists(), until the dialog takes it.
  std::optional<TargetFileExistsQuestion> m_pendingQuestion;
};

#endif
