// Copyright (C) 2013 GlavSoft LLC.
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

#include "WinHooks.h"
#include "win-system/SystemException.h"

HHOOK WinHooks::s_hooks = nullptr;
HookEventListener *WinHooks::s_eventListener = nullptr;

WinHooks::WinHooks()
{
  
}

WinHooks::~WinHooks()
{
  unregisterKeyboardHook(nullptr);
}

LRESULT CALLBACK WinHooks::lowLevelKeyboardHook(int nCode, WPARAM wParam, LPARAM lParam)
{
  if (nCode < 0) {
    return CallNextHookEx(s_hooks, nCode, wParam, lParam);
  }
  if (s_eventListener->onHookProc(nCode, wParam, lParam)) {
    // Processing is successful. Don't pass it to the next hook procedure.
    return true;
  } else {
    // Calling the CallNextHookEx function to chain to the next hook procedure.
    return CallNextHookEx(s_hooks, nCode, wParam, lParam);
  }
}

void WinHooks::registerKeyboardHook(HookEventListener *hookEventListener)
{
  unregisterKeyboardHook(nullptr);
  s_eventListener = hookEventListener;
  HINSTANCE hinst = GetModuleHandle(nullptr);
  s_hooks = SetWindowsHookEx(WH_KEYBOARD_LL, lowLevelKeyboardHook, hinst, 0);
  if (s_hooks == nullptr) {
    throw SystemException(_T("Unnable to set hooks"));
  }
}

void WinHooks::unregisterKeyboardHook(HookEventListener * /* hookEventListener */)
{
  if (s_hooks != nullptr) {
    UnhookWindowsHookEx(s_hooks);
  }
  s_hooks = nullptr;
  s_eventListener = nullptr;
}
