// Copyright (C) 2011,2012 GlavSoft LLC.
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

#ifndef _RFB_CUT_TEXT_EVENT_CLIENT_MESSAGE_H_
#define _RFB_CUT_TEXT_EVENT_CLIENT_MESSAGE_H_

#include "RfbClientToServerMessage.h"

class RfbCutTextEventClientMessage : public RfbClientToServerMessage
{
public:
  RfbCutTextEventClientMessage(const StringStorage *cutText);
  ~RfbCutTextEventClientMessage() override;

  //
  // Sends the text as a plain ClientCutText message, in Latin-1 as the
  // specification says: each character above U+00FF becomes '?'.
  //
  // Post-conditions: the whole message is written and flushed.
  // Throws Exception if the text is too long for the 32-bit size, before
  // anything is written; otherwise whatever writing throws.
  //
  void send(RfbOutputGate *output) override;

  //
  // Sends the text as TightVNC's ClientCutTextUtf8 message: its size in
  // bytes, then the text in UTF-8, without a terminator. Unpaired
  // surrogates become U+FFFD.
  //
  // Pre-conditions: the server has announced the message.
  // Post-conditions: the whole message is written and flushed.
  // Throws Exception if the text is too long for the conversion or the
  // 32-bit size, before anything is written; otherwise whatever writing
  // throws.
  //
  void sendUtf8(RfbOutputGate *output);

private:
  StringStorage m_cutText;
};

#endif
