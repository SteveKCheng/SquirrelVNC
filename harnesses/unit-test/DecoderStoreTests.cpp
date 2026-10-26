// Copyright (C) 2026 Steve Cheng.
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

//
// Unit tests of DecoderStore, which owns the decoders the core uses.
// Run under ReleaseAsan, they also check that the decoders are freed the
// way they were allocated.
//

#include "UnitTest.h"

#include "viewer-core/DecoderStore.h"
#include "viewer-core/RawDecoder.h"
#include "viewer-core/RichCursorDecoder.h"
#include "rfb/EncodingDefs.h"

void runDecoderStoreTests()
{
  LogWriter logWriter(nullptr);
  DecoderStore store(&logWriter);

  check(store.addDecoder(std::make_unique<RawDecoder>(&logWriter), 0),
        "DecoderStore adds a decoder");
  check(store.getDecoder(EncodingDefs::RAW) != nullptr,
        "DecoderStore finds the decoder added");
  check(store.addDecoder(std::make_unique<RichCursorDecoder>(&logWriter), -1),
        "DecoderStore adds a pseudo-decoder");
  // As RemoteViewerCore::enableCursorShapeUpdates() does when the
  // decoder is already there: the new one is destroyed.
  check(!store.addDecoder(std::make_unique<RichCursorDecoder>(&logWriter), -1),
        "DecoderStore refuses a second decoder of the same code");
  check(store.removeDecoder(EncodingDefs::RAW), "DecoderStore removes a decoder");
  check(store.getDecoder(EncodingDefs::RAW) == nullptr,
        "DecoderStore no longer finds the decoder removed");
  check(!store.removeDecoder(EncodingDefs::RAW),
        "DecoderStore reports a decoder it does not have");

  // So that SetEncodings is sent only when it changes something.
  check(!store.setPreferredEncoding(EncodingDefs::TIGHT) &&
        store.setPreferredEncoding(EncodingDefs::ZRLE) &&
        !store.setPreferredEncoding(EncodingDefs::ZRLE),
        "DecoderStore::setPreferredEncoding reports whether it changed anything");
  check(!store.allowCopyRect(true) && store.allowCopyRect(false) &&
        !store.allowCopyRect(false),
        "DecoderStore::allowCopyRect reports whether it changed anything");
}
