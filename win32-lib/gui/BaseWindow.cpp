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

#include "util/CommonHeader.h"
#include "BaseWindow.h"
#include "ScaledIcons.h"

BaseWindow::BaseWindow()
: m_hWnd(nullptr),
  m_hicon(nullptr),
  m_bWndCreated(false)
{
}

BaseWindow::~BaseWindow()
{
  if (m_bWndCreated && m_hWnd) {
    DestroyWindow(m_hWnd);
  }
}

void BaseWindow::setClass(const StringStorage *className)
{
  m_className = *className;
}

bool BaseWindow::createWindow(const StringStorage *windowName, DWORD style, HWND hWndParent,
                              int xPos, int yPos, int width, int height)
{
  if (m_hWnd) {
    return false;
  }
  m_windowName = *windowName;
  m_hWnd = CreateWindow(m_className.getString(), 
                        m_windowName.getString(), 
                        style, 
                        xPos, yPos, 
                        width, height, 
                        hWndParent, 
                        nullptr, 
                        GetModuleHandle(nullptr), 
                        reinterpret_cast<LPVOID>(this));
  m_bWndCreated = (m_hWnd == nullptr ? false : true);
  if (m_bWndCreated) {
      SetWindowLongPtr(m_hWnd, 
                       GWLP_USERDATA, 
                       reinterpret_cast<LONG_PTR>(this));
  }
  return true;
}

void BaseWindow::loadIcon(DWORD id)
{
  m_hicon = nullptr;
  if (IS_INTRESOURCE(id)) {
    m_hicon = ScaledIcons::getLarge(id);
    SetClassLongPtr(m_hWnd, GCLP_HICON, reinterpret_cast<LONG_PTR>(m_hicon));
    SetClassLongPtr(m_hWnd, GCLP_HICONSM,
                    reinterpret_cast<LONG_PTR>(ScaledIcons::getSmall(id)));
  } else {
    SetClassLongPtr(m_hWnd, GCLP_HICON, static_cast<LONG_PTR>(id));
  }
}

bool BaseWindow::destroyWindow()
{
  if (m_hWnd) {
    DestroyWindow(m_hWnd);
    return true;
  }
  return false;
}

void BaseWindow::show()
{
  _ASSERT(m_hWnd != 0);
  ShowWindow(m_hWnd, SW_SHOW);
}

void BaseWindow::hide()
{
  _ASSERT(m_hWnd != 0);
  ShowWindow(m_hWnd, SW_HIDE);
}

bool BaseWindow::setSize(int width, int height)
{
  _ASSERT(m_hWnd != 0);
  return !!SetWindowPos(m_hWnd, nullptr, 0, 0, width, height, 
                        SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
}

bool BaseWindow::setPosition(int xPos, int yPos)
{
  _ASSERT(m_hWnd != 0);
  return !!SetWindowPos(m_hWnd, nullptr, xPos, yPos, 0, 0, 
                        SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
}

void BaseWindow::setParent(HWND hwnd)
{
  _ASSERT(m_hWnd != 0);
  SetParent(m_hWnd, hwnd);
}

void BaseWindow::setClassCursor(HCURSOR hcursor)
{
  _ASSERT(m_hWnd != 0);
  SetClassLongPtr(m_hWnd, GCLP_HCURSOR, reinterpret_cast<LONG_PTR>(hcursor));
}

LONG BaseWindow::getStyle()
{
  _ASSERT(m_hWnd != 0);
  return GetWindowLong(m_hWnd, GWL_STYLE);
}

void BaseWindow::setStyle(DWORD style)
{
  _ASSERT(m_hWnd != 0);
  SetWindowLong(m_hWnd, GWL_STYLE, style);
}

LONG BaseWindow::getExStyle()
{
  _ASSERT(m_hWnd != 0);
  return GetWindowLong(m_hWnd, GWL_EXSTYLE);
}

void BaseWindow::setExStyle(DWORD exstyle)
{
  _ASSERT(m_hWnd != 0);
  SetWindowLong(m_hWnd, GWL_EXSTYLE, exstyle);
}

bool BaseWindow::onCommand(WPARAM /* wParam */, LPARAM /* lParam */)
{
  return false;
}

bool BaseWindow::onNotify(int /* idCtrl */, LPNMHDR /* pnmh */)
{
  return false;
}

bool BaseWindow::onSysCommand(WPARAM /* wParam */, LPARAM /* lParam */)
{
  return false;
}

bool BaseWindow::onMessage(UINT /* message */, WPARAM /* wParam */, LPARAM /* lParam */)
{
  return false;
}

bool BaseWindow::wndProc(UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message) {
    case WM_COMMAND:
      return onCommand(wParam, lParam);
    case WM_NOTIFY:
      return onNotify((int)wParam, (LPNMHDR)lParam);
    case WM_SYSCOMMAND:
      return onSysCommand(wParam, lParam);
    case WM_LBUTTONDOWN:
    case WM_LBUTTONUP:
    case WM_MBUTTONDOWN:
    case WM_MBUTTONUP:
    case WM_RBUTTONDOWN:
    case WM_RBUTTONUP:
    case WM_MOUSEWHEEL:
    case WM_MOUSEMOVE:
    {
      unsigned char mouseButtons = 0;

      mouseButtons |= LOWORD(wParam) & MK_RBUTTON ? MOUSE_RDOWN : 0;
      mouseButtons |= LOWORD(wParam) & MK_MBUTTON ? MOUSE_MDOWN : 0;
      mouseButtons |= LOWORD(wParam) & MK_LBUTTON ? MOUSE_LDOWN : 0;

      // Translate position from LPARAM to POINT.
      POINTS points = MAKEPOINTS(lParam);
      POINT point;
      point.x = points.x;
      point.y = points.y;

      unsigned short wheelSpeed = 0; 
      if (message == WM_MOUSEWHEEL) {
        // Get speed wheel and set mouse button.
        signed short wheelSignedSpeed = static_cast<signed short>(HIWORD(wParam));
        if (wheelSignedSpeed < 0) {
          mouseButtons |= MOUSE_WDOWN;
          wheelSpeed = - wheelSignedSpeed / WHEEL_DELTA;
        } else {
          mouseButtons |= MOUSE_WUP;
          wheelSpeed = wheelSignedSpeed / WHEEL_DELTA;
        }

        // In some cases wheelSignedSpeed can be smaller than the WHEEL_DELTA,
        // then wheelSpeed set to 1, but not 0.
        if (wheelSpeed == 0) {
          wheelSpeed = 1;
        }

        // If windows-message is WHEEL, then need to translate screen coordinate to client.
        if (!ScreenToClient(getHWnd(), &point)) {
          point.x = -1;
          point.y = -1;
        }
      }

      // Notify window about mouse-event.
      return onMouse(mouseButtons, static_cast<unsigned short>(wheelSpeed), point);
    }
  }
  return onMessage(message, wParam, lParam);
}

void BaseWindow::setHWnd(HWND hwnd)
{
  m_hWnd = hwnd;
}

HWND BaseWindow::getHWnd() const
{
  return m_hWnd;
}

void BaseWindow::setWindowText(const StringStorage *text)
{
  _ASSERT(m_hWnd != 0);
  SetWindowText(m_hWnd, text->getString());
}

void BaseWindow::redraw(const RECT *rcArea)
{
  _ASSERT(m_hWnd != 0);

  if (rcArea == nullptr) {
     InvalidateRect(m_hWnd, nullptr, TRUE);
  } else {
     InvalidateRect(m_hWnd, rcArea, FALSE);
  }
}

bool BaseWindow::onMouse(unsigned char /* msg */, unsigned short /* wspeed */, POINT /* pt */)
{
  return false;
}

void BaseWindow::setForegroundWindow()
{
  _ASSERT(m_hWnd != 0);

  SetForegroundWindow(m_hWnd);
}

void BaseWindow::postMessage(UINT Msg, WPARAM wParam, LPARAM lParam)
{
  _ASSERT(m_hWnd != 0);

  PostMessage(m_hWnd, Msg, wParam, lParam);
}

void BaseWindow::getClientRect(RECT *rc)
{
  _ASSERT(m_hWnd != 0 && rc);

  GetClientRect(m_hWnd, rc);
}

void BaseWindow::getBorderSize(int *width, int *height)
{
  _ASSERT(m_hWnd != 0);

  *width = 2 * GetSystemMetrics(SM_CXSIZEFRAME);
  *height = GetSystemMetrics(SM_CYSIZE) +
            2 * GetSystemMetrics(SM_CYSIZEFRAME);
}
