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

#include "SpinControl.h"
#include <CommCtrl.h>
#include "util/StringParser.h"

SpinControl::SpinControl()
: m_buddy(nullptr),
  m_isAutoAccelerationEnabled(false), m_maxDelta(0)
{
}

SpinControl::~SpinControl()
{
}

void SpinControl::setBuddy(Control *buddyControl)
{
  SendMessage(m_hwnd, UDM_SETBUDDY, (WPARAM)buddyControl->getWindow(), NULL);
  m_buddy = buddyControl;
}

void SpinControl::setRange(short lower, short upper)
{
  SendMessage(m_hwnd, UDM_SETRANGE, NULL, (LPARAM)MAKELONG(upper, lower));
}

void SpinControl::setRange32(int lower, int upper)
{
  SendMessage(m_hwnd, UDM_SETRANGE32, lower, upper);
}

void SpinControl::setAutoAccelerationParams(const std::vector<int> &limitters,
                                            const std::vector<int> &deltas,
                                            int maxDelta)
{
  m_limitters = limitters;
  m_deltas = deltas;
  m_maxDelta = maxDelta;
}
