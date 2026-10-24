// Copyright (C) 2008,2009,2010,2011,2012 GlavSoft LLC.
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

#ifndef _BASE_DIALOG_H_
#define _BASE_DIALOG_H_

#include "Control.h"
#include "util/StringStorage.h"

class BaseDialog
{
public:
  // Constructs a dialog with no resource ID yet.  The derived class must
  // call setResourceId() before create(), show() or showModal().
  BaseDialog();
  // Constructs a dialog to be loaded from the DIALOG resource resourceId.
  // The pre-condition on resourceId is as for setResourceId().
  explicit BaseDialog(WORD resourceId);
  BaseDialog(const BaseDialog &) = delete;
  BaseDialog &operator=(const BaseDialog &) = delete;
  BaseDialog(BaseDialog &&) = delete;
  BaseDialog &operator=(BaseDialog &&) = delete;

  virtual ~BaseDialog() = default;
public:

  // Method creates non modal window but not shows it
  //
  // Pre-condition: a valid resource ID has been set (see setResourceId()).
  void create();

  //
  // Methods creates windows and show it in nonmodal/modal mode
  //

  int show();
  int showModal();

  // Returns true if dialog is already created.
  bool isCreated();

  // Method hides window
  void hide();
  // Method closes dialog
  void kill(int code);
  // Method sets parent window
  void setParent(Control *ctrlParent);
  // Sets the ID of the DIALOG resource, in this executable's module, that
  // create(), show() and showModal() load the dialog template from.
  //
  // Pre-condition: id is not 0, which means "not set".  (The type is WORD
  // because integer resource IDs are 16-bit: MAKEINTRESOURCE keeps only
  // the low 16 bits.)  The ID is read only when the dialog window is
  // created, so changing it afterwards has no effect on the existing window.
  void setResourceId(WORD id);
  // Return
  Control *getControl() { return &m_ctrlThis; }
  // Setup control by ID
  //
  // Pre-condition: the dialog window has been created.  Post-condition:
  // control refers to the child with ID id, or to no window if there is none.
  void setControlById(Control &control, WORD id);
  // Icon manipulation
  //
  // loadIcon() takes the ID of an icon resource in the executable, and
  // takes effect when the dialog window is next created, or on updateIcon().
  void loadIcon(WORD id);
  void updateIcon();

  // Puts this control foreground and activates it
  bool setForeground();

protected:

  //
  // This methods must be overrided by child classes.
  //

  virtual BOOL onInitDialog() = 0;
  // Control IDs here are 16-bit, as WM_COMMAND carries them in
  // LOWORD(wParam); dialogProc() takes the same low word for WM_NOTIFY
  // and WM_DRAWITEM.
  virtual BOOL onNotify(WORD controlID, LPARAM data);
  virtual BOOL onCommand(WORD controlID, UINT notificationID) = 0;
  BOOL onClose();
  virtual BOOL onDestroy();

  //
  // This methods can be overrided by child classes.
  //

  virtual BOOL onDrawItem(WORD controlID, LPDRAWITEMSTRUCT dis);
  virtual void onMessageReceived(UINT uMsg, WPARAM wParam, LPARAM lParam);

  //
  // Window message proccessing method
  //

  static INT_PTR CALLBACK dialogProc(HWND hwnd, UINT uMsg, WPARAM wParam, LPARAM lParam);

protected:

  WORD m_resourceId;             // Id of dialog resource
  Control m_ctrlThis;           // This dialog control
  Control *m_ctrlParent;        // Parent dialog or NULL if no parent

  bool m_isModal;

  // Large and small icons cached by ScaledIcons for the life of the
  // process, so they are not destroyed.
  HICON m_hicon;
  HICON m_hiconSmall;
};

#endif
