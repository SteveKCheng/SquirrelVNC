// Copyright (C) 2011,2012 GlavSoft LLC.
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

#ifndef _TVN_VIEWER_H_
#define _TVN_VIEWER_H_

#include "AboutDialog.h"
#include "ConfigurationDialog.h"
#include "ConnectionListener.h"
#include "ControlTrayIcon.h"
#include "LoginDialog.h"
#include "OptionsDialog.h"
#include "ViewerCollector.h"
#include "win-system/WindowsApplication.h"

#include "log-writer/LogWriter.h"
#include "viewer-core/BlockingWorkPool.h"
#include "viewer-core/NetworkThread.h"
#include "thread/AutoLock.h"

#include <map>
#include <memory>

class ViewerCollector;
class ControlTrayIcon;
class LoginDialog;

class TvnViewer final : public WindowsApplication
{
public:
  TvnViewer(HINSTANCE appInstance,
            const TCHAR *windowClassName,
            const TCHAR *viewerWindowClassName);
  ~TvnViewer() override;

  //
  // show login dialog
  //
  void showLoginDialog();

  //
  // show options for listening mode
  //
  void showListeningOptions();

  //
  // show dialog "About of Viewer"
  //

  void showAboutViewer();

  //
  // show dialog with configuration of viewer
  //
  void showConfiguration();

  // newConnection(...) and startListening(...) always do copy of params (StringStorage,
  // ConnectionData and ConnectionConfig). After call this function can free memory
  // with hostName, connectionData, connectionConfig
  //
  // Starts a viewer instance for each connection accepted by the listener
  // and not yet taken: the handler of WM_USER_NEW_LISTENING. GUI thread.
  //
  void newListeningConnection();
  void newConnection(const StringStorage &hostName, const ConnectionConfig &connectionConfig);
  void newConnection(const ConnectionData &conData, const ConnectionConfig &connectionConfig);
  // Pre: listeningPort is from 0 to 65535; the main window exists (run()
  // has created it), as the listener posts to it.
  void startListening(int listeningPort);
  void stopListening();

  // Inherited from WindowsApplication
  void registerWindowClass(WNDCLASS *wndClass) override;
  static LRESULT CALLBACK wndProc(HWND hWnd, UINT msg, WPARAM wparam, LPARAM lparam);
  void createWindow(const TCHAR *className) override;
  int processMessages() override;

public:
  // this message must sended after accepted new listening-connection
  static const int WM_USER_NEW_LISTENING = WM_USER + 1;

  // this message need send if you need show login dialog
  static const int WM_USER_SHOW_LOGIN_DIALOG = WM_USER + 2;

  // this message need send if you need show configuration dialog
  static const int WM_USER_CONFIGURATION = WM_USER + 3;

  // this message need send if you need show about dialog
  static const int WM_USER_ABOUT = WM_USER + 4;

  // This message need send if you need reconnect to host.
  // LPARAM contained pointer to ConnectionData.
  static const int WM_USER_RECONNECT = WM_USER + 5;

  // This message need send if config is changed.
  static const int WM_USER_CONFIGURATION_RELOAD = WM_USER + 6;

  // This timer checks whether the application should end: no viewer
  // instance left (instances delete themselves; ViewerCollector), no login
  // dialog, and not listening.
  static const int TIMER_CHECK_SHUTDOWN = 1;
  static const int TIMER_CHECK_SHUTDOWN_DELAY = 50;

protected:
  // Starts accepting connections from servers on listeningPort.  On
  // failure, shows an error message and clears m_isListening.
  // Pre: listeningPort is from 0 to 65535; the main window exists.
  void startListeningServer(const int listeningPort);
  void stopListeningServer();
  void restartListeningServer();

  void registerViewerWindowClass();
  void unregisterViewerWindowClass();
  static LRESULT CALLBACK wndProcViewer(HWND hWnd, UINT msg, WPARAM wparam, LPARAM lparam);

  //
  // Starts a viewer instance for a copy of conData and config.
  //
  void runInstance(const ConnectionData &conData, const ConnectionConfig &config);

  // This method return true, if login dialog is visible.
  bool isVisibleLoginDialog() const;

  bool onTimer(WPARAM idTimer);

  // The network thread of every connection, whose executor each viewer
  // instance is given. Declared before m_instances, so that it is destroyed, joining the
  // thread, after them: their connections end on it. Asio holds its own
  // reference to WinSock until then, so the WindowsSocket::cleanup() in
  // ~TvnViewer() comes first without harm.
  NetworkThread m_networkThread;
  // The thread for the connections' blocking work (local deletes in file
  // transfer), started on first use; declared before m_instances for the
  // same reason: the work keeps its instance alive until it ends.
  BlockingWorkPool m_blockingWorkPool;
  ViewerCollector m_instances;

  // class name of viewer-window
  StringStorage m_viewerWindowClassName;

  HACCEL m_hAccelTable;

private:
  bool m_isListening;
  
  LogWriter m_logWriter;

  AboutDialog m_aboutDialog;
  ConfigurationDialog m_configurationDialog;
  OptionsDialog m_optionsDialog;

  std::unique_ptr<LoginDialog> m_loginDialog;
  std::unique_ptr<ControlTrayIcon> m_trayIcon;
  // Declared after m_networkThread, so that it is stopped before the
  // thread is joined; the thread's join waits for it otherwise.
  std::unique_ptr<ConnectionListener> m_conListener;

  WNDCLASS m_viewerWndClass;
};

#endif
