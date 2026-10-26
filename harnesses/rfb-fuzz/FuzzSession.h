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
#include "network/RfbInputGate.h"
#include "region/Dimension.h"
#include "rfb/FrameBuffer.h"
#include "thread/LocalMutex.h"
#include "viewer-core/CursorPainter.h"
#include "viewer-core/DecoderStore.h"
#include "io-lib/AwaitableReader.h"
#include "util/Task.h"

#include "FuzzNotifier.h"

//
// Reads one fuzz input: what a server sends after the security handshake,
// in a session where the client never changes the pixel format, that is
// ServerInit followed by server messages (notes/testing.md, "Input: a
// strict subset of RFB").
//
// FramebufferUpdate is decoded in full by the real decoders and the real
// CursorPainter, on the calling thread. Other messages the viewer handles
// are read and ignored; any other message type ends the input.
//
// The reading loop is a copy of RemoteViewerCore's, which runs on threads
// of its own; each check copied from it names the function it mirrors, and
// the two must be kept in step. After the asynchronous rewrite the core's
// loop can be called directly and this copy goes away.
//
class FuzzSession final
{
public:
  //
  // The largest frame buffer accepted, in pixels, so that runs stay fast.
  // The viewer's own limit is far larger (RemoteViewerCore::
  // setMaxFrameBufferPixels()).
  //
  static const UINT64 MAX_FRAME_BUFFER_PIXELS = 256 * 256;

  //
  // Pre-conditions: notifier outlives this object, and is never started.
  //
  FuzzSession(FuzzNotifier *notifier);
  ~FuzzSession();

  //
  // Read the whole input.
  //
  // Pre-conditions: data points to size bytes (data may be 0 if size is 0).
  // Post-conditions: the input has been read up to its end, or up to the
  // first point where the viewer would reject it; rejection (Exception) is
  // caught, as it is correct behaviour. Anything else (a crash, an ASan
  // report, a hang) is a finding. The notifier's region is empty again.
  //
  void run(const UINT8 *data, size_t size);

private:
  void readServerInit();
  PixelFormat readPixelFormat();
  void setFrameBuffer(const Dimension &dimension, const PixelFormat &pixelFormat);
  bool readMessage();
  void readFbUpdate();
  bool readFbUpdateRectangle();
  Task<> readPseudoEncoding(AwaitableReader &reader, const Rect &rect, int encodingType);
  void skipBytes(size_t length);

  FuzzNotifier *m_notifier;
  LogWriter m_logWriter;
  // Valid only during run(); they point to objects local to it.
  RfbInputGate *m_input;
  DecoderStore *m_decoderStore;
  FrameBuffer *m_frameBuffer;
  FrameBuffer *m_rectangleFb;
  CursorPainter *m_cursorPainter;

  LocalMutex m_fbLock;
};

#endif // _FUZZ_SESSION_H_
