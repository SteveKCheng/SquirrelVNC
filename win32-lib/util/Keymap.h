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

#ifndef _KEYMAP_H_
#define _KEYMAP_H_

#include "util/CommonHeader.h"

// FIXME: This class is Windows-specific so it should not reside in util/.

/**
 * FIXME: Update documentation (e.g. methods are not static any more).
 * Class with static methods that allows to make 3 type of translations:
 * 1) X11 key sym to virtual key code;
 * 2) X11 key sym to char.
 * 3) char to X11 key sym.
 */
class Keymap final
{
public:
  /**
   * Converts virtual key code to X11 key sym.
   * @param vkCode virtual key code.
   * @param keySym [out] X11 key sym.
   * @return true if virtual key code is translated into keySym, false otherwise.
   */
  bool virtualCodeToKeySym(UINT32 *keySym, BYTE vkCode);

  /**
   * Converts char to X11 key sym.
   * @param ch unicode char.
   * @param keySym [out] X11 key sym.
   * @return true if char is translated to key sym, false otherwise.
   */
  bool unicodeCharToKeySym(WCHAR ch, UINT32 *keySym);
};

#endif
