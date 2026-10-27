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

#ifndef _VIEWER_CORE_EVENTS_H_
#define _VIEWER_CORE_EVENTS_H_

#include "log-writer/LogWriter.h"
#include "viewer-core/CoreEventsAdapter.h"
#include "viewer-core/FileTransferCapability.h"

#include <functional>
#include <memory>
#include <mutex>
#include <optional>

class DesktopWindow;

//
// The part of a viewer window that its connection calls: RemoteViewerCore's
// adapter (CoreEventsAdapter), and the password prompt of
// ViewerVncAuthHandler. It runs on the network thread (the frame buffer
// events on the notifier thread), so it holds no reference to the window,
// whose members belong to the GUI thread: it keeps what the connection
// reports, and posts a message to the window for each event, whose handler
// (ViewerWindow) takes it from here on the GUI thread. It is not nested in
// ViewerWindow, which would give it access to the window's members.
//
// The adapter is inherited privately, and its events are private: only the
// core calls them, through the interface that getAdapter() gives it, and
// they cannot be called by mistake through a ViewerCoreEvents.
//
// Lifetime: a member of ViewerWindow, given to the core as the adapter
// through ViewerInstance's control block (notes/async-networking.md,
// "Ownership by std::shared_ptr").
//
class ViewerCoreEvents final : private CoreEventsAdapter
{
public:
  //
  // Called with the password, or with nothing; see requestPassword().
  //
  using PasswordReply = std::function<void(std::optional<StringStorage>)>;

  //
  // The messages posted to the window, one for each event, handled by
  // ViewerWindow. WM_USER_AUTH_ERROR has the AuthException's code in WPARAM.
  //
  static const UINT WM_USER_ERROR = WM_USER + 1;
  static const UINT WM_USER_DISCONNECT = WM_USER + 3;
  static const UINT WM_USER_AUTH_ERROR = WM_USER + 4;
  static const UINT WM_USER_CONNECTED = WM_USER + 6;
  static const UINT WM_USER_BELL = WM_USER + 7;
  static const UINT WM_USER_CUT_TEXT = WM_USER + 8;
  static const UINT WM_USER_PASSWORD = WM_USER + 9;

  //
  // desktop gets the frame buffer events, on the notifier thread
  // (DesktopWindow::updateFramebuffer() and setNewFramebuffer(), which lock
  // the frame buffer themselves); logger may be null. Nothing is posted
  // until attach().
  //
  // Pre-conditions: desktop outlives this object.
  //
  ViewerCoreEvents(DesktopWindow &desktop, Logger *logger);
  ViewerCoreEvents(const ViewerCoreEvents &) = delete;
  ViewerCoreEvents &operator=(const ViewerCoreEvents &) = delete;
  ViewerCoreEvents(ViewerCoreEvents &&) = delete;
  ViewerCoreEvents &operator=(ViewerCoreEvents &&) = delete;
  ~ViewerCoreEvents() override;

  //
  // This object as the core's adapter (RemoteViewerCore::start()), sharing
  // owner's control block (the aliasing constructor), so that the
  // connection's references keep the owner, and so this object, alive.
  //
  // Pre-conditions: owner owns this object: it is the ViewerInstance whose
  // window this belongs to. This cannot be checked.
  // Post-conditions: the result points to this object, and shares
  // ownership with owner.
  //
  std::shared_ptr<CoreEventsAdapter> getAdapter(const std::shared_ptr<void> &owner);

  //
  // Asks the user for the password of the connection, without waiting: the
  // window shows the dialog on the GUI thread (on WM_USER_PASSWORD), and
  // reply gets the answer.
  //
  // Pre-conditions: called on the network thread (by
  // ViewerVncAuthHandler), and not again before reply has been called.
  // Post-conditions: reply is called exactly once: on the GUI thread with
  // what the user entered, or with nothing if the user cancelled; or with
  // nothing, on the calling thread, if the request is cancelled by
  // cancelPasswordRequest() (before or after this call) or cannot be
  // posted (the window is gone).
  //
  void requestPassword(PasswordReply reply);

  //
  // Ends a pending requestPassword() with no password, and makes later
  // calls reply with nothing at once. Called when the window or its
  // connection ends; the dialog, if open, stays open, and its answer is
  // ignored. Any thread; reply is called on the calling thread.
  //
  // Post-conditions: no request is pending, and none will be.
  //
  void cancelPasswordRequest() noexcept;

  //
  // The rest of the public functions are for ViewerWindow, which owns this
  // object, and only it calls them.
  //

  //
  // For ViewerWindow, on the GUI thread, before the connection starts.
  //
  // Pre-conditions: window is the viewer window, just created; fileTransfer
  // outlives the connection.
  // Post-conditions: the events are posted to window, and onConnected()
  // sets up fileTransfer.
  //
  void attach(HWND window);
  void setFileTransfer(FileTransferCapability *fileTransfer);

  //
  // For ViewerWindow, when its window is being destroyed (WM_NCDESTROY):
  // nothing is posted any more, as the handle may be reused by another
  // window. GUI thread.
  //
  void detach() noexcept;

  //
  // For ViewerWindow's handlers, on the GUI thread: what the connection
  // reported. The take functions leave nothing behind; the latest error and
  // disconnect message stay.
  //
  std::optional<ConnectionInfo> takeConnectionInfo();
  std::optional<StringStorage> takeCutText();
  Exception getError() const;
  StringStorage getDisconnectMessage() const;

  //
  // For ViewerWindow's handler of WM_USER_PASSWORD, on the GUI thread:
  // whether a request is pending (it may have been cancelled meanwhile),
  // and its answer, which calls its reply on this thread, or does nothing
  // if none is pending.
  //
  // Post-conditions (answerPasswordRequest()): no request is pending.
  //
  bool hasPasswordRequest() const;
  void answerPasswordRequest(std::optional<StringStorage> password) noexcept;

private:
  //
  // The events of CoreEventsAdapter. Each keeps what it is given, if
  // anything, and posts the message of the same name; onEstablished() does
  // nothing, and the frame buffer events go to the desktop window.
  // onConnected() also gives the file transfer its output before posting,
  // so that it is ready when the window enables file transfer. The events
  // that end the connection (onDisconnect(), onAuthError(), onError()) also
  // tell the file transfer (FileTransferCapability::onConnectionEnded()).
  //
  void onBell() override;
  void onEstablished() override;
  void onConnected(RfbOutputGate *output, const ConnectionInfo &info) override;
  void onDisconnect(const StringStorage &message) override;
  void onAuthError(const AuthException &exception) override;
  void onError(const Exception &exception) override;
  void onFrameBufferUpdate(const FrameBuffer &fb, const Rect &rect) override;
  void onFrameBufferPropChange(const FrameBuffer &fb) override;
  void onCutText(const StringStorage &cutText) override;

  //
  // Posts message to the window, unless it is not attached. Returns
  // whether it was posted. The caller holds m_lock, so that detach() cannot
  // come between the check and the post.
  //
  bool post(UINT message, WPARAM wParam = 0) noexcept;

  LogWriter m_logWriter;
  DesktopWindow &m_desktop;
  // Set before the connection starts; used on the network thread then.
  FileTransferCapability *m_fileTransfer;

  // Guards what follows, up to the password request.
  mutable std::mutex m_lock;
  // The window events are posted to; null before attach() and after
  // detach().
  HWND m_window;
  // What onConnected() was given, until the window takes it.
  std::optional<ConnectionInfo> m_pendingConnectionInfo;
  // The latest cut text from the server not yet put on the clipboard. Only
  // the latest text matters, so a newer one replaces it.
  std::optional<StringStorage> m_pendingCutText;
  // The latest error, from onAuthError() or onError().
  Exception m_error;
  // The latest message of onDisconnect().
  StringStorage m_disconnectMessage;

  // The reply of the pending requestPassword(), if any, and whether
  // cancelPasswordRequest() has been called. Guarded by
  // m_passwordRequestLock: a reply is called outside it, and may post to
  // the network thread.
  PasswordReply m_passwordRequest;
  bool m_isPasswordRequestCancelled;
  mutable std::mutex m_passwordRequestLock;
};

#endif
