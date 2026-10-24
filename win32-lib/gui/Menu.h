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

#ifndef _MENU_H_
#define _MENU_H_

#include "util/CommonHeader.h"
#include <crtdbg.h>

class Menu
{
public:
  Menu();
  Menu(const Menu &) = delete;
  Menu &operator=(const Menu &) = delete;
  Menu(Menu &&) = delete;
  Menu &operator=(Menu &&) = delete;

  virtual ~Menu();

  HMENU getMenu();
  void setMenu(HMENU hmenu);
  void getSystemMenu(HWND hwnd);
  void create();
  bool loadMenu(LPCTSTR lpMenuName);
  void createPopupMenu();
  
  int getMenuItemCount();
  bool appendMenu(StringStorage strVal, UINT uID); 
  bool appendSeparator();

  bool insertMenuItem(UINT uItem, StringStorage strVal, UINT uID);
  bool insertSeparator(UINT uItem);

  bool enableMenuItem(UINT uID, UINT uEnable);
  bool checkedMenuItem(UINT uID, bool bEnable);

  int findMenuItem(UINT uID);

  bool setDefaultItem(UINT uID);

  void operator= (HMENU hmenu)
  {
    setMenu(hmenu);
  }

private:
  bool appendMenu(UINT uFlags, UINT_PTR uIDNewItem, LPCTSTR lpNewItem);
  bool insertMenuItem(UINT uItem, BOOL fByPosition, LPCMENUITEMINFO lpmii);

protected:
  HMENU m_menu;
  bool m_bCreated;
};

#endif
