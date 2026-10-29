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

#include "FuzzSession.h"

#include "io-lib/ByteArrayInputStream.h"
#include "rfb/EncodingDefs.h"
#include "rfb/MsgDefs.h"
#include "thread/AutoLock.h"
#include "viewer-core/CopyRectDecoder.h"
#include "viewer-core/DesktopSizeDecoder.h"
#include "viewer-core/HexTileDecoder.h"
#include "viewer-core/LastRectDecoder.h"
#include "viewer-core/PointerPosDecoder.h"
#include "viewer-core/RawDecoder.h"
#include "viewer-core/RfbParsing.h"
#include "viewer-core/RichCursorDecoder.h"
#include "viewer-core/RreDecoder.h"
#include "viewer-core/TightDecoder.h"
#include "viewer-core/ZrleDecoder.h"

#include <stdio.h>
#include <stdlib.h>

#include <vector>

// Shared with rfb-replay.
#include "../rfb-replay/SplitInputStream.h"

FuzzSession::FuzzSession()
: m_logWriter(nullptr),
  m_reader(nullptr),
  m_decoderStore(nullptr),
  m_frameBuffer(nullptr),
  m_cursor()
{
}

FuzzSession::~FuzzSession()
{
}

void FuzzSession::run(const UINT8 *data, size_t size)
{
  const char *bytes = reinterpret_cast<const char *>(data);
  ByteArrayInputStream whole(bytes, size);
  Outcome wholeOutcome = runOnce(whole);

  // The seed comes from the input (FNV-1a), so that a run can be repeated.
  UINT32 seed = 2166136261u;
  for (size_t i = 0; i < size; i++) {
    seed = (seed ^ data[i]) * 16777619u;
  }
  SplitInputStream split(bytes, size, MAX_SPLIT_PIECE, seed);
  Outcome splitOutcome = runOnce(split);

  if (!(wholeOutcome == splitOutcome)) {
    fprintf(stderr, "rfb-fuzz: the input split into pieces (seed %u) gave a "
                    "different result from the input whole\n", seed);
    abort();
  }
}

bool FuzzSession::Outcome::operator==(const Outcome &other) const
{
  return isRejected == other.isRejected &&
         dimension.isEqualTo(other.dimension) &&
         pixelFormat.isEqualTo(other.pixelFormat) &&
         pixels == other.pixels &&
         cursor.hotSpot.isEqualTo(other.cursor.hotSpot) &&
         cursor.width == other.cursor.width &&
         cursor.height == other.cursor.height &&
         cursor.pixels == other.cursor.pixels;
}

FuzzSession::Outcome FuzzSession::runOnce(InputStream &stream)
{
  Outcome outcome;
  AwaitableReader reader;

  // New decoders for each input, so that their state (the zlib streams of
  // Tight and ZRLE) starts fresh. Mirrors RemoteViewerCoreImpl::init().
  DecoderStore decoderStore(&m_logWriter);
  decoderStore.addDecoder(std::make_unique<RawDecoder>(&m_logWriter), 0);
  decoderStore.addDecoder(std::make_unique<CopyRectDecoder>(&m_logWriter), 10);
  decoderStore.addDecoder(std::make_unique<RreDecoder>(&m_logWriter), 1);
  decoderStore.addDecoder(std::make_unique<HexTileDecoder>(&m_logWriter), 4);
  decoderStore.addDecoder(std::make_unique<TightDecoder>(&m_logWriter), 9);
  decoderStore.addDecoder(std::make_unique<ZrleDecoder>(&m_logWriter), 9);
  decoderStore.addDecoder(std::make_unique<DesktopSizeDecoder>(&m_logWriter), -1);
  decoderStore.addDecoder(std::make_unique<LastRectDecoder>(&m_logWriter), -1);
  decoderStore.addDecoder(std::make_unique<PointerPosDecoder>(&m_logWriter), -1);
  decoderStore.addDecoder(std::make_unique<RichCursorDecoder>(&m_logWriter), -1);

  // A new frame buffer for each input too: before ServerInit it is empty.
  FrameBuffer frameBuffer;

  m_reader = &reader;
  m_frameBuffer = &frameBuffer;
  m_decoderStore = &decoderStore;
  m_cursor = CursorImage();
  try {
    // Reading the input as the core reads a connection: through the reader,
    // which runBlocking() feeds from the stream.
    runBlocking(readSession(), reader, stream);
  } catch (const Exception &) {
    // The viewer rejects the input here: correct behaviour.
    outcome.isRejected = true;
  }
  outcome.dimension = frameBuffer.getDimension();
  outcome.pixelFormat = frameBuffer.getPixelFormat();
  const char *pixels = static_cast<const char *>(frameBuffer.getBuffer());
  if (pixels != nullptr) {
    outcome.pixels.assign(pixels, pixels + frameBuffer.getBufferSize());
  }
  outcome.cursor = m_cursor;
  m_reader = nullptr;
  m_frameBuffer = nullptr;
  m_decoderStore = nullptr;
  return outcome;
}

// The whole input: ServerInit, then messages until one that the viewer
// does not handle itself, or the end of the input.
Task<> FuzzSession::readSession()
{
  co_await readServerInit();
  while (co_await readMessage()) {
  }
}

// Mirrors RemoteViewerCoreImpl::clientAndServerInit(), after the shared
// flag.
Task<> FuzzSession::readServerInit()
{
  RfbParsing::ServerInit serverInit =
    co_await RfbParsing::readServerInit(*m_reader, m_logWriter);
  std::lock_guard<std::mutex> lock(m_fbLock);
  setFrameBuffer(serverInit.dimension, serverInit.pixelFormat);
}

// Mirrors RemoteViewerCoreImpl::setFbProperties(), with the fuzzer's limit.
// Pre-conditions: m_fbLock is held.
void FuzzSession::setFrameBuffer(const Dimension &dimension,
                                 const PixelFormat &pixelFormat)
{
  RfbParsing::checkFrameBufferProperties(dimension, pixelFormat,
                                       MAX_FRAME_BUFFER_PIXELS);
  if (!m_frameBuffer->setProperties(dimension, pixelFormat)) {
    throw Exception(_T("Failed to set property frame buffer"));
  }
  m_frameBuffer->setColor(0, 0, 0);
}

// Mirrors the message loop in RemoteViewerCoreImpl::run(). Returns false
// for a message type the viewer does not handle itself, which ends the
// input.
Task<bool> FuzzSession::readMessage()
{
  UINT32 msgType = co_await RfbParsing::readServerMessageType(*m_reader);

  switch (msgType) {
  case ServerMsgDefs::FB_UPDATE:
    co_await readFbUpdate();
    co_return true;
  case ServerMsgDefs::SET_COLOR_MAP_ENTRIES:
    co_await RfbParsing::readSetColorMapEntries(*m_reader);
    co_return true;
  case ServerMsgDefs::BELL:
    co_return true;
  // The text is decoded, as the core decodes it, and dropped.
  case ServerMsgDefs::SERVER_CUT_TEXT:
    co_await RfbParsing::readServerCutText(*m_reader, m_logWriter);
    co_return true;
  case ServerMsgDefs::SERVER_CUT_TEXT_UTF8:
    co_await RfbParsing::readServerCutTextUtf8(*m_reader, m_logWriter);
    co_return true;
  default:
    co_return false;
  }
}

// Mirrors RemoteViewerCoreImpl::receiveFbUpdate().
Task<> FuzzSession::readFbUpdate()
{
  UINT16 numberOfRectangles = co_await RfbParsing::readFbUpdateHeader(*m_reader);
  bool isLastRect = false;
  for (int rectangle = 0; rectangle < numberOfRectangles && !isLastRect; rectangle++) {
    isLastRect = co_await readFbUpdateRectangle();
  }
}

// Mirrors RemoteViewerCoreImpl::receiveFbUpdateRectangle().
Task<bool> FuzzSession::readFbUpdateRectangle()
{
  RfbParsing::RectangleHeader header = co_await RfbParsing::readRectangleHeader(*m_reader);
  const Rect &rect = header.rect;
  int encodingType = header.encodingType;

  if (encodingType == PseudoEncDefs::LAST_RECT)
    co_return true;
  if (!Decoder::isPseudo(encodingType)) {
    RfbParsing::checkRectangleInside(m_frameBuffer->getDimension(), rect);

    Decoder *decoder = m_decoderStore->getDecoder(encodingType);
    if (decoder == nullptr) {
      throw Exception(_T("Decoder isn't exist"));
    }
    // As in the core, the result of the cast is not checked: every
    // decoder registered for an encoding that is not pseudo is one.
    DecoderOfRectangle *rectangleDecoder = dynamic_cast<DecoderOfRectangle *>(decoder);
    co_await rectangleDecoder->process(*m_reader, *m_frameBuffer, rect, m_fbLock);
  } else {
    co_await readPseudoEncoding(rect, encodingType);
  }
  co_return false;
}

// Mirrors RemoteViewerCoreImpl::processPseudoEncoding(), keeping the cursor
// shape that the core gives the application.
Task<> FuzzSession::readPseudoEncoding(const Rect &rect, int encodingType)
{
  switch (encodingType) {
  case PseudoEncDefs::DESKTOP_SIZE:
    {
      std::lock_guard<std::mutex> lock(m_fbLock);
      Dimension dimension(rect);
      PixelFormat pixelFormat = m_frameBuffer->getPixelFormat();
      setFrameBuffer(dimension, pixelFormat);
    }
    break;

  case PseudoEncDefs::RICH_CURSOR:
    {
      RfbParsing::RichCursor cursor =
        co_await RfbParsing::readRichCursor(*m_reader, rect,
                                          m_frameBuffer->getBytesPerPixel());

      // CoreEventsAdapter::onCursorShape()
      m_cursor = CursorImage::fromRichCursor(cursor, m_frameBuffer->getPixelFormat());
    }
    break;

  case PseudoEncDefs::POINTER_POS:
    // Only logged by the core.
    break;

  default:
    throw Exception(_T("Pseudo encoding is not supported"));
  }
}
