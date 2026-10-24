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

#ifndef _DECODER_STORE_H_
#define _DECODER_STORE_H_

#include <map>
#include <memory>
#include <vector>

#include "Decoder.h"

#include "log-writer/LogWriter.h"

class DecoderStore
{
public:
  DecoderStore(LogWriter *logWriter);
  ~DecoderStore();

  Decoder *getDecoder(INT32 decoderId);
  std::vector<INT32> getDecoderIds();
  
  //
  // Pre-conditions: newDecoder is not empty.
  // Post-conditions: returns true, and keeps newDecoder, if no decoder of
  // its code was stored; otherwise destroys newDecoder and returns false.
  //
  bool addDecoder(std::unique_ptr<Decoder> newDecoder, int priority);
  // return true, if deleting is complete
  // return false, if decoder not exist
  bool removeDecoder(INT32 decoderId);

  void setPreferredEncoding(INT32 encodingType);
  void allowCopyRect(bool allow);

private:
  LogWriter *m_logWriter;

  // For each code, the decoder's priority and the decoder.
  std::map<INT32, std::pair<int, std::unique_ptr<Decoder> > > m_decoders;
  INT32 m_preferredEncoding;
  bool m_allowCopyRect;
};

#endif
