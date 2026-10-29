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

#ifndef _VIEWER_WINDOW_H_
#define _VIEWER_WINDOW_H_

#include "ConnectionData.h"
#include "DesktopWindow.h"
#include "FileTransferEvents.h"
#include "FileTransferMainDialog.h"
#include "NamingDefs.h"
#include "OptionsDialog.h"
#include "ScaleManager.h"
#include "ViewerMenu.h"
#include "gui/ToolBar.h"
#include "log-writer/LogWriter.h"
#include "viewer-core/FileTransferCapability.h"
#include "viewer-core/RemoteViewerCore.h"
#include "viewer-core/VncAuthentication.h"
#include "ViewerCoreEvents.h"
#include "win-system/DisplayRequest.h"
#include "win-system/SystemInformation.h"
#include "win-system/WindowsApplication.h"
#include "win-system/WinHooks.h"

#include <memory>

class ViewerWindow final : public BaseWindow,
                     private HookEventListener
{
public:
  ViewerWindow(WindowsApplication *application,
               ConnectionData *conData, ConnectionConfig *conConf,
               Logger *logger = nullptr);
  ~ViewerWindow() override;

  //
  // Pre-conditions: GUI thread, before the connection starts; called once;
  // ft outlives this window.
  // Post-conditions: ft reports to this window's FileTransferEvents, and
  // is set up when the connection is (ViewerCoreEvents::onConnected()).
  //
  void setFileTransfer(FileTransferCapability *ft);
  void setRemoteViewerCore(RemoteViewerCore *pCore);

  //
  // This function return value of flag m_requiresReconnect.
  //
  bool requiresReconnect() const;

  //
  // Keeps owner, the object this window belongs to (ViewerInstance), alive
  // until the window is destroyed (WM_NCDESTROY): the Win32 "window deletes
  // itself" pattern, through a shared reference, so that a connection still
  // running keeps the owner too (notes/async-networking.md, "Ownership by
  // std::shared_ptr"). GUI thread.
  //
  // Pre-conditions: the window exists and has not been destroyed.
  // Post-conditions: m_owner holds owner until WM_NCDESTROY, which releases
  // it; the release posts the deletion, if it was the last reference, and
  // never deletes inside the window procedure.
  //
  void setOwner(std::shared_ptr<void> owner);

  //
  // Destroys the window, unless it is gone already; destroying it stops the
  // connection and cancels a pending password request (onDestroy()).
  //
  // Pre-conditions: GUI thread, with no window procedure of this window on
  // the stack (from the top of the message loop, or after it has ended).
  // Post-conditions: the window is gone, and m_owner released.
  //
  void destroy();

  //
  // The part of this window that its connection calls, on the network
  // thread: the adapter of RemoteViewerCore, and the password prompt of
  // ViewerVncAuthHandler. It lives as long as this window. GUI thread.
  //
  ViewerCoreEvents &getCoreEvents();

  // WM_USER + 1 to 4 and 6 to 9 are ViewerCoreEvents' messages.
  static const int WM_USER_FS_WARNING = WM_USER + 5;

protected:
  static const int TIMER_DESKTOP_STATE = 1;
  static const int TIMER_DESKTOP_STATE_DELAY = 50;

  bool onMessage(UINT message, WPARAM wParam, LPARAM lParam) override;
  bool onEraseBackground(HDC hdc);
  
  bool onDisconnect();
  bool onAuthError(WPARAM wParam);
  bool onError();

  //
  // The handlers of ViewerCoreEvents' messages, on the GUI thread, which
  // take what the connection reported from m_coreEvents: onDisconnect()
  // (WM_USER_DISCONNECT), onAuthError() (WM_USER_AUTH_ERROR) and onError()
  // (WM_USER_ERROR) tell the user, and destroy the window.
  //

  //
  // Handles WM_USER_CONNECTED, on the GUI thread: marks the window as
  // connected, shows it and applies the settings.
  //
  // Pre-condition: posted by ViewerCoreEvents::onConnected(), after the
  // protocol initialization of m_viewerCore has completed.
  // Post-condition: m_isConnected is true, and m_connectionInfo holds what
  // onConnected() was given.
  //
  bool handleConnected();

  //
  // Handles WM_USER_BELL, on the GUI thread: beeps, after restoring and
  // activating the window if the connection is so configured.
  //
  bool handleBell();

  //
  // Handles WM_USER_CUT_TEXT, on the GUI thread: puts the latest text from
  // the server, if not taken already, on the clipboard (if the clipboard is
  // enabled).
  //
  bool handleCutText();

  //
  // Handles WM_USER_FRAME_BUFFER, on the GUI thread: gives the desktop
  // window what changed in the frame buffer, to be painted.
  //
  bool handleFrameBufferChanges();

  //
  // Handles WM_USER_CURSOR, on the GUI thread: gives the latest cursor
  // shape from the server, if not taken already, to the desktop window.
  //
  bool handleCursorShape();

  //
  // Handles WM_USER_PASSWORD, on the GUI thread: shows AuthenticationDialog,
  // unless the request has been cancelled meanwhile, and answers the request
  // with what the user entered, or nothing if the user cancelled (or the
  // dialog threw, after which the exception propagates).
  //
  // Post-conditions: no request is pending.
  //
  bool handlePasswordRequest();

  bool onFsWarning();
  bool onSize(WPARAM wParam, LPARAM lParam);
  bool onCreate(LPCREATESTRUCT lps);
  bool onCommand(WPARAM wParam, LPARAM lParam) override;
  bool onNotify(WORD idCtrl, LPNMHDR pnmh) override;
  bool onSysCommand(WPARAM wParam, LPARAM lParam) override;
  bool onClose();
  // WM_DESTROY: stops the connection and the file transfer's current
  // operation, without waiting; they keep the instance alive until they
  // have ended.
  bool onDestroy();
  bool onFocus(WPARAM wParam);
  bool onKillFocus(WPARAM wParam);
  bool onTimer(WPARAM idTimer);

  void desktopStateUpdate();
  void commandCtrlAltDel();
  void commandCtrlEsc();
  void commandCtrl();
  void commandAlt();
  void commandToolBar();
  void commandPause();
  void onAbout();
  void commandNewConnection();
  void commandSaveSession();
  void commandScaleIn();
  void commandScaleOut();
  void commandScale100();
  void commandScaleAuto();

  int translateAccelToTB(int val);
  void applyScreenChanges(bool isFullScreen);
  
  // function return default rect of viewer window:
  // if size of remote screen is more local desktop, then return rect of desktop
  // else return rect of remote screen + border
  Rect calculateDefaultSize();

  LogWriter m_logWriter;

  Control m_control;

  ConnectionConfigSM m_ccsm;
  ConnectionConfig *m_conConf;
  WindowsApplication *m_application;
  RemoteViewerCore *m_viewerCore;
  FileTransferCapability *m_fileTransfer;
  // The file transfer's interface, for the life of the window; the dialog
  // attaches to it while it exists.
  FileTransferEvents m_ftEvents;
  std::unique_ptr<FileTransferMainDialog> m_ftDialog;
  DesktopWindow m_dsktWnd;
  // Called by the connection; posts to this window (getCoreEvents()).
  ViewerCoreEvents m_coreEvents;
  StringStorage m_strToolTip;
  ToolBar m_toolbar;
  ViewerMenu m_menu;
  ConnectionData *m_conData;
  SystemInformation m_sysinf;

  // The connection's details, once handleConnected() has run; empty
  // before. Accessed only on the GUI thread.
  ConnectionInfo m_connectionInfo;

  // Flag is set, if now viewer is in full screen mode
  bool m_isFullScr;
  // It's size of work-area in windowed mode. It is necessary for restore size of window.
  WINDOWPLACEMENT m_workArea;
  // It's size of optimal size of work-area in windowed mode.
  Rect m_rcNormal;


  // Flag is set after recv first message WM_SIZING.
  bool m_sizeIsChanged;
  // Flag is set, if toolbar is visible.
  bool m_bToolBar;
  // It is scale of viewer window in percent.
  int m_scale;

  // Flag is set by handleConnected(). Accessed only on the GUI thread.
  bool m_isConnected;

  // Flag is set, if instance is requires to reconnect.
  bool m_requiresReconnect;

  // Set when the window has been destroyed (WM_NCDESTROY). Accessed only on
  // the GUI thread.
  bool m_stopped;

  // The owner of this window, kept alive until WM_NCDESTROY (setOwner()).
  // Accessed only on the GUI thread.
  std::shared_ptr<void> m_owner;
private:
  std::vector<int> m_standardScale;
  void changeCursor(int type);
  void applySettings();
  void setSizeFullScreenWindow();
  void doFullScr();
  void doUnFullScr();
  void doSize();
  //
  // The file-transfer operations the server supports, from the
  // connection's capabilities: none before handleConnected().
  //
  OperationSupport getFileTransferSupport() const;

  //
  // Shows the file-transfer dialog if the toolbar button is down, creating
  // it anew if its window was closed. The dialog uses m_ftEvents, which
  // outlives it.
  //
  // Pre-conditions: setFileTransfer() has been called.
  //
  void showFileTransferDialog();
  void showWindow();
  void enableUserElements();
  bool viewerCoreSettings();
  void dialogConnectionOptions();
  void dialogConnectionInfo();
  void switchFullScreenMode();
  void dialogConfiguration();
  void adjustWindowSize();
  StringStorage formatWindowName() const;
  void updateKeyState();

  // Returns true if the display should be kept on while connected.
  bool wantsDisplayKeptOn();
  // Activates or clears m_displayRequest to match the connection state
  // and wantsDisplayKeptOn().
  void updateDisplayRequest();

  // onHookProc function implementation of HookEventListener base abstract class.
  LRESULT onHookProc(int code, WPARAM wParam, LPARAM lParam) override;
  WinHooks m_winHooks;
  bool m_hooksEnabledFirstTime;

  // Keeps the display on (no screen saver, display power-off or lock
  // screen) while connected, if wantsDisplayKeptOn() returns true.
  DisplayRequest m_displayRequest;
};

#endif
