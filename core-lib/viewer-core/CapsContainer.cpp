// Copyright (C) 2012 GlavSoft LLC.
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

#include "CapsContainer.h"

CapsContainer::CapsContainer()
{
}

CapsContainer::~CapsContainer()
{
}

void CapsContainer::add(UINT32 code, const char *vendor, const char *name,
                        const StringStorage desc)
{
  // Fill in an RfbCapabilityInfo structure and pass it to the overloaded
  // function.
  RfbCapabilityInfo capinfo;
  capinfo.code = code;
  memcpy(capinfo.vendorSignature, vendor, RfbCapabilityInfo::vendorSigSize);
  memcpy(capinfo.nameSignature, name, RfbCapabilityInfo::nameSigSize);
  add(capinfo, desc);
}

void CapsContainer::add(const RfbCapabilityInfo &capinfo,
                        const StringStorage desc)
{
  infoMap[capinfo.code] = capinfo;
  enableMap[capinfo.code] = false;
  descMap[capinfo.code] = desc;
}

bool CapsContainer::enable(const RfbCapabilityInfo &capinfo)
{
  if (!isKnown(capinfo.code)) {
    return false;
  }

  const RfbCapabilityInfo known = infoMap[capinfo.code];
  if (!known.isEqual(capinfo.vendorSignature, capinfo.nameSignature)){
    enableMap[capinfo.code] = false;
    return false;
  }

  enableMap[capinfo.code] = true;
  m_plist.push_back(capinfo.code);
  return true;
}

bool CapsContainer::isEnabled(UINT32 code) const
{
  return (isKnown(code)) ? enableMap.find(code)->second : false;
}

size_t CapsContainer::numEnabled() const
{
  return m_plist.size();
}

UINT32 CapsContainer::getByOrder(size_t idx)
{
  return (idx < m_plist.size()) ? m_plist[idx] : 0;
}

void CapsContainer::getEnabledCapabilities(std::vector<UINT32> &codes) const
{
  codes = m_plist;
}

bool CapsContainer::isKnown(UINT32 code) const
{
  return (descMap.find(code) != descMap.end());
}

bool RfbCapabilityInfo::isEqual(const char *vendor, const char *signature) const
{
  if (memcmp(vendorSignature, vendor, vendorSigSize) != 0 ||
      memcmp(nameSignature, signature, nameSigSize) != 0 ) {
    return false;
  }
  return true;
}
