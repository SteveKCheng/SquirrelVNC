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

#include "util/Exception.h"
#include "util/TextEncoding.h"

#include <cstdint>


#include "RfbCutTextEventClientMessage.h"

RfbCutTextEventClientMessage::RfbCutTextEventClientMessage(const StringStorage &cutText)
: m_cutText(cutText)
{
}

RfbCutTextEventClientMessage::~RfbCutTextEventClientMessage()
{
}

void RfbCutTextEventClientMessage::send(RfbOutputGate *output)
{
  // Latin-1, as the specification says.
  std::vector<std::byte> cutTextLatin1 = TextEncoding::toLatin1(m_cutText);
  if (cutTextLatin1.size() > UINT32_MAX) {
    throw Exception(_T("Cut text is too long to send"));
  }
  UINT32 length = static_cast<UINT32>(cutTextLatin1.size());

  AutoLock al(output);
  output->writeUInt8(ClientMsgDefs::CLIENT_CUT_TEXT);
  output->writeUInt8(0); // padding 3 bytes
  output->writeUInt8(0);
  output->writeUInt8(0);
  output->writeUInt32(length);
  output->writeFully(cutTextLatin1.data(), length);
  output->flush();
}

void RfbCutTextEventClientMessage::sendUtf8(RfbOutputGate *output)
{
  std::vector<std::byte> cutTextUtf8 = TextEncoding::toUtf8(m_cutText);
  if (cutTextUtf8.size() > UINT32_MAX) {
    throw Exception(_T("Cut text is too long to send"));
  }
  UINT32 length = static_cast<UINT32>(cutTextUtf8.size());

  AutoLock al(output);
  output->writeUInt32(ClientMsgDefs::CLIENT_CUT_TEXT_UTF8);
  output->writeUInt32(length);
  output->writeFully(cutTextUtf8.data(), length);
  output->flush();
}
