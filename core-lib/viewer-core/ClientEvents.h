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

#ifndef _CLIENT_EVENTS_H_
#define _CLIENT_EVENTS_H_

#include "region/Point.h"
#include "rfb/PixelFormat.h"
#include "util/StringStorage.h"

#include <deque>
#include <optional>
#include <utility>
#include <variant>

//
// Internal: by convention, for RemoteViewerCore and its unit tests only.
// It is a named namespace rather than an anonymous one so that the tests
// can reach it. Callers use RemoteViewerCore's methods, which make these
// events, so that they do not depend on this representation.
//
namespace Internal
{
  //
  // Events from the GUI side: each public method of RemoteViewerCore that
  // sends a client message, or changes a setting of the session, makes one
  // and posts it to the network thread, where a handler of the core does
  // what the method stands for (notes/async-networking.md, Phase 4 step
  // 2e).
  //
  struct KeyEvent
  {
    bool isDown;
    UINT32 keySym;
  };

  struct PointerEvent
  {
    UINT8 buttonMask;
    Point position;
  };

  struct CutTextEvent
  {
    StringStorage text;
  };

  struct StopUpdatingEvent
  {
    bool isStopped;
  };

  struct RefreshEvent
  {
  };

  struct PixelFormatEvent
  {
    PixelFormat pixelFormat;
  };

  struct PreferredEncodingEvent
  {
    INT32 encodingType;
  };

  struct CopyRectEvent
  {
    bool isAllowed;
  };

  struct JpegQualityEvent
  {
    int level;
  };

  struct CompressionLevelEvent
  {
    int level;
  };

  struct CursorShapeUpdatesEvent
  {
    bool isEnabled;
  };

  struct IgnoreCursorShapeUpdatesEvent
  {
    bool isIgnored;
  };

  using ClientEvent = std::variant<KeyEvent,
                                   PointerEvent,
                                   CutTextEvent,
                                   StopUpdatingEvent,
                                   RefreshEvent,
                                   PixelFormatEvent,
                                   PreferredEncodingEvent,
                                   CopyRectEvent,
                                   JpegQualityEvent,
                                   CompressionLevelEvent,
                                   CursorShapeUpdatesEvent,
                                   IgnoreCursorShapeUpdatesEvent>;

  //
  // Whether event changes the set of decoders. Such an event waits for the
  // next message boundary: between messages no decoder is in use, while a
  // suspended rectangle may hold the decoder it would remove.
  //
  inline bool changesDecoderStore(const ClientEvent &event)
  {
    return std::holds_alternative<PreferredEncodingEvent>(event) ||
           std::holds_alternative<CopyRectEvent>(event) ||
           std::holds_alternative<JpegQualityEvent>(event) ||
           std::holds_alternative<CompressionLevelEvent>(event) ||
           std::holds_alternative<CursorShapeUpdatesEvent>(event);
  }

  //
  // The input events (KeyEvent, PointerEvent, CutTextEvent) waiting for
  // the writer of a connection, in order, with pointer moves coalesced:
  // while the writer waits for a slow link, only the latest position of a
  // move matters. Used on one thread.
  //
  class InputEventQueue final
  {
  public:
    InputEventQueue()
    : m_isLastMove(false)
    {
    }

    //
    // Appends event; a pointer move replaces the position of a pointer
    // move just before it, instead. A move is a PointerEvent whose button
    // mask is that of the pointer event before it (pushed earlier, taken
    // or not), so that a press or release, the position where it happened,
    // and the order of moves against keys and cut text, are all kept.
    //
    // Pre-conditions: event is a KeyEvent, PointerEvent or CutTextEvent.
    // Post-conditions: the queue ends with event, or with the move it was
    // merged into, which now has its position.
    //
    void push(ClientEvent event)
    {
      const PointerEvent *pointer = std::get_if<PointerEvent>(&event);
      if (pointer == nullptr) {
        m_events.push_back(std::move(event));
        m_isLastMove = false;
        return;
      }
      bool isMove = m_lastButtonMask == pointer->buttonMask;
      m_lastButtonMask = pointer->buttonMask;
      if (isMove && m_isLastMove) {
        std::get<PointerEvent>(m_events.back()).position = pointer->position;
        return;
      }
      m_events.push_back(std::move(event));
      m_isLastMove = isMove;
    }

    //
    // Post-conditions: returns the events in the order they were pushed,
    // and the queue is empty; a move pushed next cannot merge into them,
    // but is still told from a press or release by the last button mask.
    //
    std::deque<ClientEvent> take()
    {
      m_isLastMove = false;
      return std::exchange(m_events, {});
    }

  private:
    std::deque<ClientEvent> m_events;
    // The button mask of the last PointerEvent pushed; none before the
    // first.
    std::optional<UINT8> m_lastButtonMask;
    // Whether the last event in m_events is a pointer move.
    bool m_isLastMove;
  };
}

#endif
