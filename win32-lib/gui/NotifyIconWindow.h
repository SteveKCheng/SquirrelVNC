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

#ifndef _NOTIFY_ICON_WINDOW_H_
#define _NOTIFY_ICON_WINDOW_H_

#include "util/CommonHeader.h"

#include "WindowProcHolder.h"

class NotifyIconWindow
{
protected:
  // Protected: only a subclass makes one.
  NotifyIconWindow();

public:
  NotifyIconWindow(const NotifyIconWindow &) = delete;
  NotifyIconWindow &operator=(const NotifyIconWindow &) = delete;
  NotifyIconWindow(NotifyIconWindow &&) = delete;
  NotifyIconWindow &operator=(NotifyIconWindow &&) = delete;

  virtual ~NotifyIconWindow();

  HWND getWindow();

  void setWindowProcHolder(WindowProcHolder *wph);
  
protected:
  HWND m_window;
  WindowProcHolder *m_wph;

  friend class NotifyIcon;
};

#endif
