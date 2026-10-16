// Copyright (C) 2026 Steve Cheng.
//
//-------------------------------------------------------------------------
// This file is part of a modified version of the TightVNC software.
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

#include "DisplayRequest.h"
#include "SystemException.h"
#include "thread/AutoLock.h"

DisplayRequest::DisplayRequest()
: m_request(INVALID_HANDLE_VALUE)
{
}

DisplayRequest::~DisplayRequest()
{
  deactivate();
}

void DisplayRequest::activate(const TCHAR *reason)
{
  AutoLock al(&m_lock);
  if (m_request != INVALID_HANDLE_VALUE) {
    return;
  }

  // The reason string is only read during the call. REASON_CONTEXT takes
  // a wide string, so this requires a Unicode build.
  REASON_CONTEXT context = {};
  context.Version = POWER_REQUEST_CONTEXT_VERSION;
  context.Flags = POWER_REQUEST_CONTEXT_SIMPLE_STRING;
  context.Reason.SimpleReasonString = const_cast<LPWSTR>(reason);

  HANDLE request = PowerCreateRequest(&context);
  if (request == INVALID_HANDLE_VALUE) {
    throw SystemException(_T("Cannot create a power request"));
  }
  if (!PowerSetRequest(request, PowerRequestDisplayRequired)) {
    // Construct the exception first: it reads GetLastError().
    SystemException e(_T("Cannot set a display-required power request"));
    CloseHandle(request);
    throw e;
  }
  m_request = request;
}

void DisplayRequest::deactivate()
{
  AutoLock al(&m_lock);
  if (m_request == INVALID_HANDLE_VALUE) {
    return;
  }
  PowerClearRequest(m_request, PowerRequestDisplayRequired);
  CloseHandle(m_request);
  m_request = INVALID_HANDLE_VALUE;
}

bool DisplayRequest::isActive()
{
  AutoLock al(&m_lock);
  return m_request != INVALID_HANDLE_VALUE;
}
