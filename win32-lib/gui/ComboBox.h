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

#ifndef _COMBOBOX_H_
#define _COMBOBOX_H_

#include "util/CommonHeader.h"
#include "Control.h"

class ComboBox final : public Control
{
public:
  ComboBox();
  ~ComboBox() override;

  // Adds new item to the end of combo box items list.
  int addItem(const TCHAR *text);

  // Adds new item to the end of combo box items list, with user data
  // (tag) as for setItemData().  Returns the new item's index, or
  // CB_ERR or CB_ERRSPACE if the item could not be added, in which case
  // no data is set.
  int addItem(const TCHAR *text, LPARAM tag);

  // Inserts new item to the specified position
  void insertItem(int index, const TCHAR *text);

  // Inserts new item to the specified position, or at the end if index is
  // -1, with user data (tag) as for setItemData().  If the item could not
  // be inserted, no data is set.
  void insertItem(int index, const TCHAR *text, LPARAM tag);

  // Returns count of combo box items
  int getItemsCount() const;

  // Sets user data (tag) associated with combo box item with specified
  // index.  The tag is an LPARAM, as the control stores it (and as in
  // ListView), so an integer tag needs no cast to or from a pointer type;
  // a pointer tag goes through reinterpret_cast.  An item's tag is 0 until
  // set.  Does nothing if index is not a valid item
  // index.
  void setItemData(int index, LPARAM tag);

  // Returns user data associated with combo box item with specified index,
  // or CB_ERR (-1) if index is not a valid item index.  Callers that store
  // -1 as a tag cannot tell the two apart.
  LPARAM getItemData(int index) const;

  // Sets text associated with combo box item with specified index
  void getItemText(int index, StringStorage *storage) const;

  // Returns current selected item index
  int getSelectedItemIndex();

  // Selects item with specified index
  void setSelectedItem(int index);

  // Removes all combo box items
  void removeAllItems();
};

#endif
