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

#include "ViewerSettingsManager.h"

#include "util/Exception.h"
#include "win-system/Registry.h"

std::unique_ptr<SettingsManager> ViewerSettingsManager::s_instance;

ViewerSettingsManager::ViewerSettingsManager(const TCHAR registryName[])
: RegistrySettingsManager()
{
  StringStorage keyName;
  keyName.format(_T("%s\\Settings\\"), registryName);
  setRegistryKey(Registry::getCurrentUserKey(), keyName.getString());
}

ViewerSettingsManager::~ViewerSettingsManager()
{
}

void ViewerSettingsManager::initInstance(const TCHAR registryName[])
{
  s_instance = std::make_unique<ViewerSettingsManager>(registryName);
}

SettingsManager *ViewerSettingsManager::getInstance()
{
  if (!s_instance) {
    throw Exception(_T("Instance of viewer settings manager is 0"));
  }
  return s_instance.get();
}
