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

#ifndef _FUZZ_SESSION_H_
#define _FUZZ_SESSION_H_

#include "log-writer/LogWriter.h"
#include "region/Dimension.h"
#include "rfb/FrameBuffer.h"
#include "thread/LocalMutex.h"
#include "viewer-core/CursorPainter.h"
#include "viewer-core/DecoderStore.h"
#include "io-lib/AwaitableReader.h"
#include "util/Task.h"

#include "FuzzNotifier.h"

#include <vector>

//
// Reads one fuzz input: what a server sends after the security handshake,
// in a session where the client never changes the pixel format, that is
// ServerInit followed by server messages (notes/testing.md, "Input: a
// strict subset of RFB").
//
// FramebufferUpdate is decoded in full by the real decoders and the real
// CursorPainter, on the calling thread. Other messages the viewer handles
// are read, with their text decoded, and ignored; any other message type
// ends the input.
//
// Messages are parsed by the functions the core uses (RfbParsing.h). What
// is done with them is a copy of RemoteViewerCoreImpl's: the message
// loop, the dispatch of rectangles to the decoders, and the frame buffers
// and cursor; each function names the one it mirrors, and the two must be
// kept in step. This copy is to be revisited after the rework of the
// frame buffer (notes/cpu-usage.md, "What to do").
//
// The handshake and authentication are deliberately left out: inputs made
// at random fail in more ways when everything is mixed in, which makes
// findings harder to triage. They are for a fuzzer of whole sessions.
//
class FuzzSession final
{
public:
  //
  // The largest frame buffer accepted, in pixels, so that runs stay fast.
  // The viewer's own limit is far larger (the maxFrameBufferPixels
  // argument of RemoteViewerCore::start()).
  //
  static const UINT64 MAX_FRAME_BUFFER_PIXELS = 256 * 256;

  //
  // Pre-conditions: notifier outlives this object, and is never started.
  //
  FuzzSession(FuzzNotifier *notifier);
  ~FuzzSession();

  //
  // The largest piece in which run() gives the input the second time.
  //
  static const size_t MAX_SPLIT_PIECE = 64;

  //
  // Read the whole input, twice: given as the reader asks for it, then in
  // pieces of random sizes up to MAX_SPLIT_PIECE bytes, from a seed taken
  // from the input. The protocol code suspends whenever its reader runs
  // short, so the second run stops and resumes it at many points
  // (notes/async-networking.md, Phase 3); both runs must end the same way,
  // with the same frame buffer, or run() aborts: a finding.
  //
  // Pre-conditions: data points to size bytes (data may be 0 if size is 0).
  // Post-conditions: the input has been read up to its end, or up to the
  // first point where the viewer would reject it; rejection (Exception) is
  // caught, as it is correct behaviour. Anything else (a crash, an ASan
  // report, a hang, the two runs differing) is a finding. The notifier's
  // region is empty again.
  //
  void run(const UINT8 *data, size_t size);

private:
  //
  // How one run ended: whether the input was rejected, and the frame
  // buffer at the end.
  //
  struct Outcome
  {
    bool isRejected = false;
    Dimension dimension;
    PixelFormat pixelFormat;
    std::vector<char> pixels;

    bool operator==(const Outcome &other) const;
  };

  //
  // Reads the input from stream once, as run() describes, with new
  // decoders and frame buffers.
  //
  Outcome runOnce(InputStream &stream);

  Task<> readSession();
  Task<> readServerInit();
  void setFrameBuffer(const Dimension &dimension, const PixelFormat &pixelFormat);
  Task<bool> readMessage();
  Task<> readFbUpdate();
  Task<bool> readFbUpdateRectangle();
  Task<> readPseudoEncoding(const Rect &rect, int encodingType);

  FuzzNotifier *m_notifier;
  // Without a logger: it logs nothing.
  LogWriter m_logWriter;
  // Valid only during run(); they point to objects local to it.
  // What the coroutines read: the input, fed by runBlocking().
  AwaitableReader *m_reader;
  DecoderStore *m_decoderStore;
  FrameBuffer *m_frameBuffer;
  FrameBuffer *m_rectangleFb;
  CursorPainter *m_cursorPainter;

  LocalMutex m_fbLock;
};

#endif // _FUZZ_SESSION_H_
