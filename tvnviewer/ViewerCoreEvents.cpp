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

#include "ViewerCoreEvents.h"

#include "DesktopWindow.h"

#include <utility>

ViewerCoreEvents::ViewerCoreEvents(DesktopWindow &desktop, Logger *logger)
: m_logWriter(logger),
  m_desktop(desktop),
  m_fileTransfer(nullptr),
  m_window(nullptr),
  m_isPasswordRequestCancelled(false)
{
}

ViewerCoreEvents::~ViewerCoreEvents()
{
}

std::shared_ptr<CoreEventsAdapter> ViewerCoreEvents::getAdapter(const std::shared_ptr<void> &owner)
{
  return std::shared_ptr<CoreEventsAdapter>(owner, this);
}

void ViewerCoreEvents::attach(HWND window)
{
  std::lock_guard<std::mutex> lock(m_lock);
  m_window = window;
}

void ViewerCoreEvents::setFileTransfer(FileTransferCapability *fileTransfer)
{
  m_fileTransfer = fileTransfer;
}

void ViewerCoreEvents::detach() noexcept
{
  std::lock_guard<std::mutex> lock(m_lock);
  m_window = nullptr;
}

bool ViewerCoreEvents::post(UINT message, WPARAM wParam) noexcept
{
  return m_window != nullptr && PostMessage(m_window, message, wParam, 0) != 0;
}

void ViewerCoreEvents::onBell()
{
  std::lock_guard<std::mutex> lock(m_lock);
  post(WM_USER_BELL);
}

void ViewerCoreEvents::onEstablished()
{
  // Nothing to do until the protocol is initialized (onConnected()).
}

void ViewerCoreEvents::onConnected(RfbOutputGate *output, const ConnectionInfo &info)
{
  // Set output for client-to-server messages in file transfer, before the
  // GUI thread can start an operation.
  m_fileTransfer->setOutput(output);

  std::lock_guard<std::mutex> lock(m_lock);
  m_pendingConnectionInfo = info;
  post(WM_USER_CONNECTED);
}

void ViewerCoreEvents::onDisconnect(const StringStorage &message)
{
  m_logWriter.info(_T("onDisconnect: %s"), message.getString());
  m_fileTransfer->onConnectionEnded();
  std::lock_guard<std::mutex> lock(m_lock);
  m_disconnectMessage = message;
  post(WM_USER_DISCONNECT);
}

void ViewerCoreEvents::onAuthError(const AuthException &exception)
{
  m_logWriter.info(_T("onAuthError (%d): %s"),
                   exception.getAuthCode(), exception.getMessage());
  m_fileTransfer->onConnectionEnded();
  std::lock_guard<std::mutex> lock(m_lock);
  m_error = exception;
  post(WM_USER_AUTH_ERROR, exception.getAuthCode());
}

void ViewerCoreEvents::onError(const Exception &exception)
{
  m_fileTransfer->onConnectionEnded();
  std::lock_guard<std::mutex> lock(m_lock);
  m_error = exception;
  post(WM_USER_ERROR);
}

void ViewerCoreEvents::onFrameBufferUpdate(const FrameBuffer &fb, const Rect &rect)
{
  m_desktop.updateFramebuffer(fb, rect);
}

void ViewerCoreEvents::onFrameBufferPropChange(const FrameBuffer &fb)
{
  m_desktop.setNewFramebuffer(fb);
}

void ViewerCoreEvents::onCutText(const StringStorage &cutText)
{
  std::lock_guard<std::mutex> lock(m_lock);
  m_pendingCutText = cutText;
  // Posted every time, so that a lost message cannot strand a later text;
  // the window does nothing for the messages after the first.
  post(WM_USER_CUT_TEXT);
}

std::optional<ConnectionInfo> ViewerCoreEvents::takeConnectionInfo()
{
  std::lock_guard<std::mutex> lock(m_lock);
  return std::exchange(m_pendingConnectionInfo, std::nullopt);
}

std::optional<StringStorage> ViewerCoreEvents::takeCutText()
{
  std::lock_guard<std::mutex> lock(m_lock);
  return std::exchange(m_pendingCutText, std::nullopt);
}

Exception ViewerCoreEvents::getError() const
{
  std::lock_guard<std::mutex> lock(m_lock);
  return m_error;
}

StringStorage ViewerCoreEvents::getDisconnectMessage() const
{
  std::lock_guard<std::mutex> lock(m_lock);
  return m_disconnectMessage;
}

void ViewerCoreEvents::requestPassword(PasswordReply reply)
{
  bool isCancelled;
  {
    std::lock_guard<std::mutex> lock(m_passwordRequestLock);
    isCancelled = m_isPasswordRequestCancelled;
    if (!isCancelled) {
      m_passwordRequest = std::move(reply);
    }
  }
  if (isCancelled) {
    reply(std::nullopt);
    return;
  }
  bool isPosted;
  {
    std::lock_guard<std::mutex> lock(m_lock);
    isPosted = post(WM_USER_PASSWORD);
  }
  // Otherwise the request would never be answered.
  if (!isPosted) {
    answerPasswordRequest(std::nullopt);
  }
}

void ViewerCoreEvents::cancelPasswordRequest() noexcept
{
  {
    std::lock_guard<std::mutex> lock(m_passwordRequestLock);
    m_isPasswordRequestCancelled = true;
  }
  answerPasswordRequest(std::nullopt);
}

bool ViewerCoreEvents::hasPasswordRequest() const
{
  std::lock_guard<std::mutex> lock(m_passwordRequestLock);
  return static_cast<bool>(m_passwordRequest);
}

void ViewerCoreEvents::answerPasswordRequest(std::optional<StringStorage> password) noexcept
{
  PasswordReply reply;
  {
    std::lock_guard<std::mutex> lock(m_passwordRequestLock);
    reply = std::exchange(m_passwordRequest, nullptr);
  }
  // Outside the lock: the reply may post to the network thread.
  if (reply) {
    reply(std::move(password));
  }
}
