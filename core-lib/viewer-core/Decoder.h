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

#ifndef _DECODER_H_
#define _DECODER_H_

#include "log-writer/LogWriter.h"
#include "network/RfbInputGate.h"
#include "region/Rect.h"
#include "rfb/FrameBuffer.h"
#include "rfb/EncodingDefs.h"
#include "thread/AutoLock.h"
#include "thread/LocalMutex.h"

class Decoder
{
public:
  // Post-conditions: getCode() returns encoding, the code of the encoding
  // or pseudo-encoding that this decoder handles.
  Decoder(LogWriter *logWriter, int encoding);
  virtual ~Decoder();

  //
  // This method return encoding of this Decoder.
  //
  int getCode() const;

  //
  // This method return true, if decoder responsible for pseudo encoding.
  //
  virtual bool isPseudo() const;

  //
  // This static method return true, if "encoding" is pseudo encoding.
  //
  static bool isPseudo(int encoding);

protected:
  LogWriter *m_logWriter;

  const int m_encoding;
};

#endif
