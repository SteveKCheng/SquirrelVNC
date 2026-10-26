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
#include "rfb/StandardPixelFormatFactory.h"
#include "thread/AutoLock.h"
#include "viewer-core/CopyRectDecoder.h"
#include "viewer-core/DesktopSizeDecoder.h"
#include "viewer-core/HexTileDecoder.h"
#include "viewer-core/LastRectDecoder.h"
#include "viewer-core/PointerPosDecoder.h"
#include "viewer-core/RawDecoder.h"
#include "viewer-core/RichCursorDecoder.h"
#include "viewer-core/RreDecoder.h"
#include "viewer-core/TightDecoder.h"
#include "viewer-core/ViewerBufferSize.h"
#include "viewer-core/ZrleDecoder.h"

#include <stdio.h>
#include <stdlib.h>

#include <vector>

// Shared with rfb-replay.
#include "../rfb-replay/SplitInputStream.h"

// As in RemoteViewerCore.cpp.
static const UINT32 MAX_DESKTOP_NAME_LENGTH = 64 * 1024;

FuzzSession::FuzzSession(FuzzNotifier *notifier)
: m_notifier(notifier),
  m_logWriter(nullptr),
  m_reader(nullptr),
  m_decoderStore(nullptr),
  m_frameBuffer(nullptr),
  m_rectangleFb(nullptr),
  m_cursorPainter(nullptr)
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
         pixels == other.pixels;
}

FuzzSession::Outcome FuzzSession::runOnce(InputStream &stream)
{
  Outcome outcome;
  AwaitableReader reader;

  // New decoders for each input, so that their state (the zlib streams of
  // Tight and ZRLE) starts fresh. Mirrors RemoteViewerCore::init().
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
  FrameBuffer rectangleFb;
  CursorPainter cursorPainter(&frameBuffer, &m_logWriter);

  m_reader = &reader;
  m_frameBuffer = &frameBuffer;
  m_rectangleFb = &rectangleFb;
  m_decoderStore = &decoderStore;
  m_cursorPainter = &cursorPainter;
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
  m_reader = nullptr;
  m_frameBuffer = nullptr;
  m_rectangleFb = nullptr;
  m_decoderStore = nullptr;
  m_cursorPainter = nullptr;
  m_notifier->clearUpdates();
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

// Mirrors RemoteViewerCore::clientAndServerInit().
Task<> FuzzSession::readServerInit()
{
  UINT16 width = co_await m_reader->readUInt16();
  UINT16 height = co_await m_reader->readUInt16();
  Dimension screenDimension(width, height);
  PixelFormat serverPixelFormat = co_await readPixelFormat();
  {
    AutoLock al(&m_fbLock);
    setFrameBuffer(screenDimension, serverPixelFormat);
  }

  UINT32 sizeInBytes = co_await m_reader->readUInt32();
  if (sizeInBytes > MAX_DESKTOP_NAME_LENGTH) {
    throw Exception(_T("Desktop name is too long"));
  }
  co_await skipBytes(sizeInBytes);
}

// Mirrors RemoteViewerCore::readPixelFormat().
Task<PixelFormat> FuzzSession::readPixelFormat()
{
  PixelFormat pixelFormat;
  pixelFormat.bitsPerPixel = co_await m_reader->readUInt8();
  pixelFormat.colorDepth = co_await m_reader->readUInt8();
  pixelFormat.bigEndian = !!co_await m_reader->readUInt8();
  bool isTrueColour = co_await m_reader->readUInt8() != 0;
  pixelFormat.redMax = co_await m_reader->readUInt16();
  pixelFormat.greenMax = co_await m_reader->readUInt16();
  pixelFormat.blueMax = co_await m_reader->readUInt16();
  pixelFormat.redShift = co_await m_reader->readUInt8();
  pixelFormat.greenShift = co_await m_reader->readUInt8();
  pixelFormat.blueShift = co_await m_reader->readUInt8();
  co_await m_reader->readUInt8(); // padding bytes (3)
  co_await m_reader->readUInt8();
  co_await m_reader->readUInt8();

  const unsigned short bitsPerPixel = pixelFormat.bitsPerPixel;
  if (bitsPerPixel != 8 && bitsPerPixel != 16 && bitsPerPixel != 32) {
    throw Exception(_T("Only 8, 16 or 32 bits per pixel supported!"));
  }
  if (!isTrueColour) {
    co_return StandardPixelFormatFactory::createPixelFormat(bitsPerPixel);
  }
  if (!pixelFormat.isValid()) {
    throw Exception(_T("The server's pixel format is invalid"));
  }
  co_return pixelFormat;
}

// Mirrors RemoteViewerCore::setFbProperties(), with the fuzzer's limit.
// Pre-conditions: m_fbLock is held.
void FuzzSession::setFrameBuffer(const Dimension &dimension,
                                 const PixelFormat &pixelFormat)
{
  const int width = dimension.width;
  const int height = dimension.height;
  if (width < 0 || height < 0 || !pixelFormat.isValid()) {
    throw Exception(_T("Invalid framebuffer properties"));
  }
  if (static_cast<UINT64>(width) * height > MAX_FRAME_BUFFER_PIXELS ||
      !ViewerBufferSize::fitsFrameBuffer(width, height,
                                         pixelFormat.bitsPerPixel / 8)) {
    throw Exception(_T("Framebuffer is too large"));
  }
  if (!m_frameBuffer->setProperties(dimension, pixelFormat) ||
      !m_rectangleFb->setProperties(dimension, pixelFormat)) {
    throw Exception(_T("Failed to set property frame buffer"));
  }
  m_rectangleFb->setColor(0, 0, 0);
  m_frameBuffer->setColor(0, 0, 0);
}

// Mirrors RemoteViewerCore::receiveServerMessageType() and the message
// loop in RemoteViewerCore::execute(). Returns false for a message type
// the viewer does not handle itself, which ends the input.
Task<bool> FuzzSession::readMessage()
{
  static const UINT16 SERVER_MSG_SPECIAL_TIGHT_CODE = 0xFC;
  UINT32 msgType = co_await m_reader->readUInt8();
  if (msgType == SERVER_MSG_SPECIAL_TIGHT_CODE) {
    for (int i = 0; i < 3; i++) {
      msgType <<= 8;
      msgType += co_await m_reader->readUInt8();
    }
  }

  switch (msgType) {
  case ServerMsgDefs::FB_UPDATE:
    co_await readFbUpdate();
    co_return true;
  case ServerMsgDefs::SET_COLOR_MAP_ENTRIES:
    {
      // Mirrors RemoteViewerCore::receiveSetColorMapEntries().
      co_await m_reader->readUInt8();
      co_await m_reader->readUInt16();
      UINT16 numberOfColours = co_await m_reader->readUInt16();
      co_await skipBytes(static_cast<size_t>(numberOfColours) * 6);
    }
    co_return true;
  case ServerMsgDefs::BELL:
    co_return true;
  case ServerMsgDefs::SERVER_CUT_TEXT:
    // Mirrors the reading in RemoteViewerCore::receiveServerCutText().
    co_await m_reader->readUInt16();
    co_await m_reader->readUInt8();
    co_await skipBytes(co_await m_reader->readUInt32());
    co_return true;
  case ServerMsgDefs::SERVER_CUT_TEXT_UTF8:
    // Mirrors the reading in RemoteViewerCore::receiveServerCutTextUtf8().
    co_await skipBytes(co_await m_reader->readUInt32());
    co_return true;
  default:
    co_return false;
  }
}

// Mirrors RemoteViewerCore::receiveFbUpdate(), then does what
// FbUpdateNotifier::execute() does with the cursor after an update.
Task<> FuzzSession::readFbUpdate()
{
  co_await m_reader->readUInt8();
  UINT16 numberOfRectangles = co_await m_reader->readUInt16();
  bool isLastRect = false;
  for (int rectangle = 0; rectangle < numberOfRectangles && !isLastRect; rectangle++) {
    isLastRect = co_await readFbUpdateRectangle();
  }

  AutoLock al(&m_fbLock);
  m_cursorPainter->showCursor();
  m_cursorPainter->hideCursor();
}

// Mirrors RemoteViewerCore::receiveFbUpdateRectangle().
Task<bool> FuzzSession::readFbUpdateRectangle()
{
  Rect rect;
  rect.left = co_await m_reader->readUInt16();
  rect.top = co_await m_reader->readUInt16();
  rect.setWidth(co_await m_reader->readUInt16());
  rect.setHeight(co_await m_reader->readUInt16());

  int encodingType = co_await m_reader->readInt32();

  if (encodingType == PseudoEncDefs::LAST_RECT)
    co_return true;
  if (!Decoder::isPseudo(encodingType)) {
    if (!m_frameBuffer->getDimension().getRect().intersection(rect).isEqualTo(rect)) {
      throw Exception(_T("Error in protocol: incorrect size of rectangle"));
    }

    Decoder *decoder = m_decoderStore->getDecoder(encodingType);
    if (decoder == nullptr) {
      throw Exception(_T("Decoder isn't exist"));
    }
    // As in the core, the result of the cast is not checked: every
    // decoder registered for an encoding that is not pseudo is one.
    DecoderOfRectangle *rectangleDecoder = dynamic_cast<DecoderOfRectangle *>(decoder);
    co_await rectangleDecoder->process(*m_reader,
                                       *m_frameBuffer, *m_rectangleFb, rect, &m_fbLock,
                                       m_notifier);
  } else {
    co_await readPseudoEncoding(rect, encodingType);
  }
  co_return false;
}

// Mirrors RemoteViewerCore::processPseudoEncoding(), with the calls the
// core makes on FbUpdateNotifier made on the CursorPainter directly, as
// the notifier does.
Task<> FuzzSession::readPseudoEncoding(const Rect &rect, int encodingType)
{
  switch (encodingType) {
  case PseudoEncDefs::DESKTOP_SIZE:
    {
      AutoLock al(&m_fbLock);
      Dimension dimension(rect);
      PixelFormat pixelFormat = m_frameBuffer->getPixelFormat();
      setFrameBuffer(dimension, pixelFormat);
    }
    break;

  case PseudoEncDefs::RICH_CURSOR:
    {
      UINT16 width = static_cast<UINT16>(rect.getWidth());
      UINT16 height = static_cast<UINT16>(rect.getHeight());
      UINT8 bytesPerPixel = m_frameBuffer->getBytesPerPixel();

      if (width > ViewerBufferSize::MAX_CURSOR_SIZE ||
          height > ViewerBufferSize::MAX_CURSOR_SIZE) {
        throw Exception(_T("Rich cursor is too large"));
      }

      std::vector<UINT8> cursor;
      std::vector<UINT8> bitmask;

      size_t cursorLen = width * height * bytesPerPixel;
      if (cursorLen != 0) {
        cursor.resize(cursorLen);
        co_await m_reader->readFully(&cursor.front(), cursorLen);

        size_t bitmaskLen = ((width + 7) / 8) * height;
        bitmask.resize(bitmaskLen);
        co_await m_reader->readFully(&bitmask.front(), bitmaskLen);
      }
      Point hotSpot(rect.left, rect.top);

      // FbUpdateNotifier::setNewCursor()
      AutoLock al(&m_fbLock);
      m_cursorPainter->setNewCursor(hotSpot, width, height, cursor, bitmask);
    }
    break;

  case PseudoEncDefs::POINTER_POS:
    {
      Point position(rect.left, rect.top);
      // FbUpdateNotifier::updatePointerPos()
      m_cursorPainter->updatePointerPos(position);
    }
    break;

  default:
    throw Exception(_T("Pseudo encoding is not supported"));
  }
}

// Mirrors RemoteViewerCore::skipBytes().
Task<> FuzzSession::skipBytes(size_t length)
{
  char buffer[4096];
  while (length > 0) {
    size_t chunk = length < sizeof(buffer) ? length : sizeof(buffer);
    co_await m_reader->readFully(buffer, chunk);
    length -= chunk;
  }
}
