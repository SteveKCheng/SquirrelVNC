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

#ifndef _FILE_TRANSFER_MAIN_DIALOG_H_
#define _FILE_TRANSFER_MAIN_DIALOG_H_

#include "gui/BaseDialog.h"
#include "gui/Control.h"
#include "gui/TextBox.h"
#include "gui/ComboBox.h"
#include "gui/ProgressBar.h"

#include "ft-common/FileInfo.h"

#include "io-lib/IOException.h"

#include "FileInfoListView.h"
#include "FileExistDialog.h"
#include "FileTransferEvents.h"
#include "ft-client-lib/FileTransferCore.h"
#include "ft-client-lib/OperationSupport.h"
#include "viewer-core/FileTransferCapability.h"

#include <functional>
#include <vector>

//
// The file-transfer dialog, modeless. It runs on the GUI thread, and never
// touches the operations, which run on the network thread: it posts its
// commands there (FileTransferCapability::post()), and the operations
// report to it through events (FileTransferEvents), which post messages to
// its window. It keeps its own state: whether an operation is running, and
// whether the connection has ended.
//
class FileTransferMainDialog final : public BaseDialog
{
public:
  //
  // supportedOps: what the server supports, from the connection's
  // capabilities.
  //
  // Pre-conditions: fileTransfer and events outlive this dialog; events is
  // the interface fileTransfer reports to
  // (FileTransferCapability::setInterface()).
  //
  FileTransferMainDialog(FileTransferCapability &fileTransfer,
                         FileTransferEvents &events,
                         const OperationSupport &supportedOps);
  ~FileTransferMainDialog() override;

  //
  // Kill main dialog.
  // Function return false, if closing dialog is canceled, and true else
  //
  bool tryClose();

  //
  // For FileTransferEvents, which passes on the event of
  // FileTransferInterface of the same name. It is called on the network
  // thread, and shows a modal dialog there: an interim, until the question
  // is posted (notes/async-networking.md, Phase 4 step 5c). Returns one of
  // CopyFileEventListener's TFE_* values.
  //
  int onFtTargetFileExists(FileInfo *sourceFileInfo,
                           FileInfo *targetFileInfo,
                           const TCHAR *pathToTargetFile);

protected:

  //
  // Inherited from BaseDialog
  //

  //
  // onInitDialog() attaches the dialog to its events before it starts the
  // first file list; onDestroy() detaches it, so nothing more is posted to
  // the window.
  //
  BOOL onInitDialog() override;
  BOOL onNotify(WORD controlID, LPARAM data) override;
  BOOL onCommand(WORD controlID, UINT notificationID) override;
  BOOL onDestroy() override;

  //
  // Handles the messages FileTransferEvents posts, taking what it keeps
  // for them.
  //
  void onMessageReceived(UINT uMsg, WPARAM wParam, LPARAM lParam) override;

  //
  // Button event handlers
  //

  void onCancelButtonClick();
  void onCancelOperationButtonClick();

  void onRenameRemoteButtonClick();
  void onMkDirRemoteButtonClick();
  void onRemoveRemoteButtonClick();
  void onRefreshRemoteButtonClick();

  void onRenameLocalButtonClick();
  void onMkDirLocalButtonClick();
  void onRemoveLocalButtonClick();
  void onRefreshLocalButtonClick();

  void onUploadButtonClick();
  void onDownloadButtonClick();

  void moveUpLocalFolder();
  void moveUpRemoteFolder();

  //
  // List view event handlers
  //

  void onRemoteListViewDoubleClick();
  void onLocalListViewDoubleClick();

  void onRemoteListViewKeyDown(UINT key);
  void onLocalListViewKeyDown(UINT key);

  //
  // Enables or disables rename and delete buttons
  // depending of file list views selected items count.
  //

  void checkRemoteListViewSelection();
  void checkLocalListViewSelection();

  //
  // Text notification methods
  //

  void insertMessageIntoComboBox(const TCHAR *message);

private:

  //
  // Sets the progress bar; progress is in [0.0, 1.0].
  //
  void setProgress(double progress);

  //
  // Starts an operation: the dialog is busy, its controls are disabled at
  // once, so that no second operation can be started before the first has
  // reported its start, and command is posted to the network thread.
  //
  // Pre-conditions: not busy, and the connection has not ended.
  //
  void startOperation(std::function<void(FileTransferCore &)> command);

  //
  // Handles the end of an operation (WM_FT_FINISHED): tells the core,
  // then, unless the dialog is closing, shows its result or refreshes the
  // list it changed. state and result are those of
  // FileTransferInterface::onFtOpFinished().
  //
  // Post-conditions: not busy, unless a refresh has started.
  //
  void onOperationFinished(int state, int result);

  //
  // Handles the end of the connection (WM_FT_CONNECTION_ENDED): nothing
  // more can be done with the server, so the controls stay disabled; the
  // dialog can still be closed.
  //
  // Post-conditions: not busy.
  //
  void onConnectionEnded();

  //
  // Shows files in the remote list view, which points into
  // m_remoteFilesInfo.
  //
  void showRemoteFileList(std::vector<FileInfo> files);

  //
  // Post-conditions: returns copies of the file info of the items
  // selected in listView, in their order there.
  //
  static std::vector<FileInfo> getSelectedFilesInfo(FileInfoListView *listView);

  //
  // Enables or disables all controls from m_controlsToBlock list. Once the
  // connection has ended, they are disabled whatever enabled is.
  //

  void enableControls(bool enabled);

  //
  // Links gui control class members with windows in dialog
  //

  void initControls();


  //
  // Refreshes local file list
  //

  void refreshLocalFileList();

  //
  // Refreshes remote file list
  //

  void refreshRemoteFileList();

  //
  // Displays file list of pathToFile folder of local machine
  // to local file list view, which points into m_localFilesInfo. On the
  // GUI thread: the local file system needs no operation.
  //

  void tryListLocalFolder(const TCHAR *pathToFile);

  //
  // Sends file list request to server and shows result
  // in remote file list view. Does nothing if the dialog is busy or the
  // connection has ended.
  //

  void tryListRemoteFolder(const TCHAR *pathToFile);

  //
  // Filenames helper methods
  //

  //
  // FIXME: Make classes for getPathTo*** methods
  //

  void getPathToCurrentLocalFolder(StringStorage *out);
  void getPathToParentLocalFolder(StringStorage *out);
  void getPathToSelectedLocalFile(StringStorage *out);

  void getPathToCurrentRemoteFolder(StringStorage *out);
  void getPathToParentRemoteFolder(StringStorage *out);
  void getPathToSelectedRemoteFile(StringStorage *out);

protected:
  FileTransferCapability &m_fileTransfer;
  // Attached while the dialog's window exists.
  FileTransferEvents &m_events;
  OperationSupport m_supportedOps;

  // Whether an operation has been started and has not reported its end.
  bool m_isBusy;
  // Whether the connection has ended (WM_FT_CONNECTION_ENDED).
  bool m_isConnectionEnded;

  // What the list views show: they point into these.
  std::vector<FileInfo> m_localFilesInfo;
  std::vector<FileInfo> m_remoteFilesInfo;

  //
  // True if window state is closing.
  //

  bool m_isClosing;



  StringStorage m_lastSentFileListPath;
  StringStorage m_lastReceivedFileListPath;

  //
  // Buttons
  //

  Control m_renameRemoteButton;
  Control m_mkDirRemoteButton;
  Control m_removeRemoteButton;
  Control m_refreshRemoteButton;

  Control m_renameLocalButton;
  Control m_mkDirLocalButton;
  Control m_removeLocalButton;
  Control m_refreshLocalButton;

  Control m_uploadButton;
  Control m_downloadButton;

  Control m_cancelButton;

  //
  // Progress bar
  //

  ProgressBar m_copyProgressBar;

  //
  // Combo box
  //

  ComboBox m_logComboBox;

  //
  // Text boxes
  //

  TextBox m_localCurFolderTextBox;
  TextBox m_remoteCurFolderTextBox;

  //
  // Tables
  //

  FileInfoListView m_localFileListView;
  FileInfoListView m_remoteFileListView;

  //
  // Helper modal dialogs
  //

  FileExistDialog m_fileExistDialog;

  //
  // File info of ".." fake folder
  //

  FileInfo m_fakeMoveUpFolder;
};

#endif
