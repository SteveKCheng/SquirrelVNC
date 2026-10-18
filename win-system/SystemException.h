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

#ifndef _SYSTEM_EXCEPTION_H_
#define _SYSTEM_EXCEPTION_H_

#include "util/Exception.h"

/**
 * Windows exception.
 *
 * Solves problem with generating formatted message strings width describes
 * user code-space where error occured and windows specific information about WinAPI error.
 */
class SystemException : public Exception
{
public:
  /**
   * Creates exception with formatted message from system
   * and error code which equals to GetLastError() value.
   */
  SystemException();
  /**
   * Creates exception with formatted message from system
   * and error code.
   * @param errcode windows error code.
   */
  SystemException(int errcode);
  /**
   * Creates exception with user message + formatted message from system
   * and error code set to GetLastError() value.
   * @param userMessage user message.
   */
  SystemException(const TCHAR *userMessage);
  /**
   * Creates exception with user message + formatted message from system
   * and specified error code.
   * @param userMessage user message.
   * @param errcode windows error code.
   */
  SystemException(const TCHAR *userMessage, int errcode);
  /**
   * Destructor, does nothing.
   */
  virtual ~SystemException();
private:
  /**
   * Sets m_systemMessage and m_message for the exception.
   *
   * m_systemMessage is the system's description of errcode with the code
   * appended, without line breaks, tabs or a final full stop, or just the
   * code if the system has no description.  m_message is m_systemMessage,
   * or, if userMessage is given, userMessage followed by m_systemMessage in
   * parentheses.
   *
   * If errcode is 0 (ERROR_SUCCESS), both are set to userMessage alone, or
   * to a fixed message saying that the error is unknown if userMessage is
   * null too.
   *
   * @param userMessage description of what failed, or null.  It is copied,
   *   not used as a format string.
   * @param errcode Windows error code, or an HRESULT.
   */
  void createMessage(const TCHAR *userMessage, int errcode);
private:
  /**
   * Description of error from OS.
   */
  StringStorage m_systemMessage;
  /**
   * Windows error code.
   */
  int m_errcode;
};

#endif
