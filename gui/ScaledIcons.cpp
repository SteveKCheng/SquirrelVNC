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

#include "ScaledIcons.h"
#include "thread/AutoLock.h"
#include "thread/LocalMutex.h"

#include <commctrl.h>
#include <map>

namespace {

struct IconKey
{
  UINT id;
  int width;
  int height;

  bool operator<(const IconKey &other) const
  {
    if (id != other.id) {
      return id < other.id;
    }
    if (width != other.width) {
      return width < other.width;
    }
    return height < other.height;
  }
};

// Function-local statics, so they are constructed on first use rather
// than in an unspecified order with other globals.
LocalMutex &cacheLock()
{
  static LocalMutex lock;
  return lock;
}

std::map<IconKey, HICON> &cache()
{
  static std::map<IconKey, HICON> icons;
  return icons;
}

} // namespace

HICON ScaledIcons::getLarge(UINT id)
{
  return get(id, GetSystemMetrics(SM_CXICON), GetSystemMetrics(SM_CYICON));
}

HICON ScaledIcons::getSmall(UINT id)
{
  return get(id, GetSystemMetrics(SM_CXSMICON), GetSystemMetrics(SM_CYSMICON));
}

HICON ScaledIcons::get(UINT id, int width, int height)
{
  AutoLock al(&cacheLock());
  IconKey key = { id, width, height };
  std::map<IconKey, HICON>::iterator it = cache().find(key);
  if (it != cache().end()) {
    return it->second;
  }

  HINSTANCE instance = GetModuleHandle(NULL);
  HICON icon = NULL;
  if (FAILED(LoadIconWithScaleDown(instance, MAKEINTRESOURCE(id), width, height, &icon))) {
    // Shared icon, which must not be destroyed either.
    icon = LoadIcon(instance, MAKEINTRESOURCE(id));
  }
  if (icon != NULL) {
    cache()[key] = icon;
  }
  return icon;
}
