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

#include <algorithm>
#include <functional>

#include "DecoderStore.h"

DecoderStore::DecoderStore(LogWriter *logWriter)
: m_logWriter(logWriter),
  m_preferredEncoding(EncodingDefs::TIGHT),
  m_allowCopyRect(true)
{
}

DecoderStore::~DecoderStore()
{
  // The decoders are freed with m_decoders.
  try {
    for (const auto &decoder : m_decoders) {
      m_logWriter->detail(_T("Decoder '%d' destroyed"), decoder.first);
    }
  } catch (...) {
  }
}

Decoder *DecoderStore::getDecoder(INT32 decoderId)
{
  auto found = m_decoders.find(decoderId);
  if (found != m_decoders.end())
    return found->second.second.get();
  else
    return 0;
}

std::vector<INT32> DecoderStore::getDecoderIds()
{
  // this method returned list of decoders, sorted by priority.
  // in first position is preffered encoding.
  std::vector<std::pair<int, INT32> > decoders;

  for (auto i = m_decoders.begin(); i != m_decoders.end(); i++) {
    // preferred encoding is skipping
    if (i->first != m_preferredEncoding) {
      // copy rect is allowed?
      if (i->first != EncodingDefs::COPYRECT || m_allowCopyRect)
        decoders.push_back(std::make_pair(i->second.first, i->first));
    }
  }
  std::sort(decoders.begin(), decoders.end(), std::greater<std::pair<int,INT32> >());
  std::vector<INT32> sortedDecoders;
  auto priorityEnc = m_decoders.find(m_preferredEncoding);
  if (priorityEnc != m_decoders.end())
    sortedDecoders.push_back(priorityEnc->first);
  for (std::vector<std::pair<INT32, int> >::iterator i = decoders.begin();
       i != decoders.end();
       i++) {
    sortedDecoders.push_back(i->second);
  }
  if (sortedDecoders.empty())
    sortedDecoders.push_back(EncodingDefs::RAW);
  return sortedDecoders;
}

bool DecoderStore::addDecoder(std::unique_ptr<Decoder> decoder, int priority)
{
  INT32 code = decoder->getCode();
  m_logWriter->detail(_T("Decoder %d added"), code);
  // Does nothing, and decoder is freed, if the code is already there.
  return m_decoders.try_emplace(code, priority, std::move(decoder)).second;
}

bool DecoderStore::removeDecoder(INT32 decoderId)
{
  if (m_decoders.erase(decoderId) != 0) {
    m_logWriter->detail(_T("Decoder '%d' destroyed (removed from list)"), decoderId);
    return true;
  }
  return false;
}

void DecoderStore::setPreferredEncoding(INT32 encodingType)
{
  m_logWriter->detail(_T("Decoder store: preferred encoding is \"%d\"."), encodingType);
  m_preferredEncoding = encodingType;
}

void DecoderStore::allowCopyRect(bool allow)
{
  if (allow) {
    m_logWriter->detail(_T("Decoder store: enable Copy Rect"));
  } else {
    m_logWriter->detail(_T("Decoder store: disable Copy Rect"));
  }
  m_allowCopyRect = allow;
}
