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

#ifndef _SCALED_ICONS_H_
#define _SCALED_ICONS_H_

#include "util/CommonHeader.h"

/**
 * Loads icons from the executable's resources at the pixel sizes that
 * the current DPI calls for, instead of the 32x32 that LoadIcon() picks.
 * An exact size in the resource is used as it is; otherwise the next
 * larger image is scaled down.
 *
 * Icons are cached for the life of the process, like the shared icons
 * LoadIcon() returns, so callers must not destroy them. This lets them be
 * set as class icons, which outlive any one window. The process is system
 * DPI aware, so the sizes do not change while it runs.
 */
class ScaledIcons
{
public:
  /**
   * Icon for a window's title bar and Alt+Tab (SM_CXICON x SM_CYICON).
   */
  static HICON getLarge(UINT id);

  /**
   * Icon for the title bar, taskbar and notification area
   * (SM_CXSMICON x SM_CYSMICON).
   */
  static HICON getSmall(UINT id);

  /**
   * Icon of the given size in pixels. Falls back to LoadIcon() if the
   * icon cannot be loaded at that size.
   */
  static HICON get(UINT id, int width, int height);
};

#endif // _SCALED_ICONS_H_
