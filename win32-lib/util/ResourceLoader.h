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

#ifndef _RESOURCE_LOADER_H_
#define _RESOURCE_LOADER_H_

#include "winhdr.h"

#include "StringStorage.h"

/**
 * Loader of resources from resource files linked with application.
 *
 * One instance, of the executable's resources, made on first use.
 *
 * Threads: getInstance() may be called from any thread; the loading
 * functions too, as they only call the system's.
 */
class ResourceLoader final
{
public:
  ResourceLoader(const ResourceLoader &) = delete;
  ResourceLoader &operator=(const ResourceLoader &) = delete;
  ResourceLoader(ResourceLoader &&) = delete;
  ResourceLoader &operator=(ResourceLoader &&) = delete;

  /**
   * The instance for the executable's module (GetModuleHandle(nullptr)),
   * made on the first call, and destroyed at exit.
   *
   * Post-conditions: never returns nullptr.
   */
  static ResourceLoader *getInstance();

  /**
   * Loads icon from resources.
   * @param iconName icon name.
   * @return icon handle of 0 if cannot load icon.
   */
  HICON loadIcon(const TCHAR *iconName);

  /**
   * Loads string from resources.
   * @param [in] id identifier of string in resource file.
   * @param [out] string storage for string from resources.
   * @return true if string is loaded, false otherwise.
   */
  bool loadString(WORD id, StringStorage *string);

  /**
   * Loads accelerator from resources.
   * @param [in] id identifier of accelerator in resource file.
   * @return handle of accelerator if it's loaded.
   */
  HACCEL loadAccelerator(WORD id);

  /**
   * Loads standart cursor.
   * @param [in] id identifier of accelerator in resource file.
   * @return handle of cursor if it's loaded.
   */
  HCURSOR loadStandardCursor(const TCHAR *id);
  /**
   * Loads cursor from resources.
   * @param [in] id identifier of cursor in resource file.
   * @return handle of cursor if it's loaded.
   */
  HCURSOR loadCursor(WORD id);

private:
  /**
   * @param appInst windwos application instance.
   */
  ResourceLoader(HINSTANCE appInst);

  /**
   * Application instance.
   */
  HINSTANCE m_appInstance;
};

#endif
