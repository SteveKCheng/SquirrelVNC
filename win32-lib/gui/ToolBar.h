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

#ifndef _TOOL_BAR_H_
#define _TOOL_BAR_H_

#include "util/CommonHeader.h"
#include <CommCtrl.h>

class ToolBar final
{
public:
  ToolBar();
  ToolBar(const ToolBar &) = delete;
  ToolBar &operator=(const ToolBar &) = delete;
  ToolBar(ToolBar &&) = delete;
  ToolBar &operator=(ToolBar &&) = delete;

  ~ToolBar();

  static const int TB_Style_sep = 0;
  static const int TB_Style_gap = 1;

   ///////////////////////////////////////////////////////// 
  // Auto mode procedures
  /////////////////////////////////////////////////////////

  // setViewAutoButtons()
  // It used only for auto buttons creation from
  // icons and making gaps or separators.
  void setViewAutoButtons(int iButton, int style);

  // loadIcons()
  // Sets the icons for the buttons: count icon resources with
  // consecutive ids starting from firstId, one per button. They are
  // loaded at the size of small icons for the current DPI. firstId
  // is also the toolbar's control id.
  //
  // Pre-condition: called once, before attachToolBar(); count >= 0 and
  // firstId + count - 1 <= 0xFFFF, as resource IDs are 16-bit.
  void loadIcons(WORD firstId, int count);

  // setButtonsRange()
  // If we want to catch the message from toolbar that some buttons
  // are pressed then we must set a range for message, we pass only
  // first item, and next is id+1 and so on.
  //
  // Pre-condition: called before attachToolBar(); 0 means the buttons send
  // no commands; otherwise id + (number of buttons) - 1 <= 0xFFFF, since
  // WM_COMMAND carries 16-bit command IDs.
  void setButtonsRange(WORD id);

  // attachToolBar()
  // This one will create and attach toolbar window to 
  // which handle is passed in hwnd. Only for toolbar from loadIcons().
  void attachToolBar(HWND hwnd);

  ///////////////////////////////////////////////////////// 
  // Manual mode procedures
  /////////////////////////////////////////////////////////

  // checkButton() checks or unchecks a given button in a toolbar control.
  bool checkButton(WORD idButton, bool check);

  // enableButton() enables or disables the specified button 
  // in the toolbar.
  bool enableButton(WORD idButton, bool enable);

  // getButtonRect() gets the bounding rectangle of a button in a toolbar.
  bool getButtonRect(int nIndex, LPRECT buttonRect);

  // autoSize() resizes the toolbar window.
  void autoSize();

  // getButtonsHeight() retrieves the height of the toolbar buttons.
  int getButtonsHeight();

  // isVisible() check the toolbar window on visible.
  bool isVisible();

  // hide() hides the toolbar window.
  void hide();

  // show() displays the toolbar window.
  void show();

  // getHeight() returns the toolbar window height.
  int getHeight();

  // getState() gets button state
  LRESULT getState(WORD idButton);

private:
  WORD m_initialStr;
  int m_numberTB;
  WORD m_id;
  int m_width, m_height;
  HWND m_hWndToolbar;
  HIMAGELIST m_imageList;

  std::map<int, int> m_autoButtons;
};

#endif
