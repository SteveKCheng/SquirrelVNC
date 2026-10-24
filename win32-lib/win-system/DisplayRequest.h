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

#ifndef _DISPLAY_REQUEST_H_
#define _DISPLAY_REQUEST_H_

#include "util/CommonHeader.h"
#include "thread/LocalMutex.h"

/**
 * Asks Windows to keep the display on while active. This also stops the
 * screen saver, and the lock screen that follows the display timeout,
 * from starting.
 *
 * Implemented with a power request, so it does not depend on the calling
 * thread, and "powercfg /requests" lists it with its reason string.
 * The request is cleared when the object is destroyed or the process exits.
 */
class DisplayRequest
{
public:
  DisplayRequest();
  DisplayRequest(const DisplayRequest &) = delete;
  DisplayRequest &operator=(const DisplayRequest &) = delete;
  DisplayRequest(DisplayRequest &&) = delete;
  DisplayRequest &operator=(DisplayRequest &&) = delete;

  virtual ~DisplayRequest();

  /**
   * Activates the request. Does nothing if it is already active.
   * @param reason description shown by "powercfg /requests".
   * @throws SystemException if Windows refuses the request.
   */
  void activate(const TCHAR *reason);

  /**
   * Clears the request. Does nothing if it is not active.
   */
  void deactivate();

  bool isActive();

private:
  LocalMutex m_lock;
  // Power request handle, or INVALID_HANDLE_VALUE when not active.
  HANDLE m_request;
};

#endif // _DISPLAY_REQUEST_H_
