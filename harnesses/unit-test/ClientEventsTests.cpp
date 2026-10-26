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
// Tests of Internal::InputEventQueue (viewer-core/ClientEvents.h): the
// order of input events, and which pointer moves are coalesced.
//

#include "UnitTest.h"

#include "viewer-core/ClientEvents.h"

#include <deque>

using Internal::ClientEvent;
using Internal::InputEventQueue;
using Internal::KeyEvent;
using Internal::PointerEvent;

namespace
{
  PointerEvent pointer(UINT8 buttonMask, int x, int y)
  {
    return PointerEvent{buttonMask, Point(x, y)};
  }

  // Whether event is the pointer event of buttonMask at (x, y).
  bool isPointer(const ClientEvent &event, UINT8 buttonMask, int x, int y)
  {
    const PointerEvent *p = std::get_if<PointerEvent>(&event);
    return p != nullptr && p->buttonMask == buttonMask &&
           p->position.x == x && p->position.y == y;
  }
}

void runClientEventsTests()
{
  {
    InputEventQueue queue;
    queue.push(pointer(0, 1, 1));
    queue.push(pointer(0, 2, 2));
    queue.push(pointer(0, 3, 3));
    queue.push(pointer(0, 4, 4));
    std::deque<ClientEvent> events = queue.take();
    check(events.size() == 2 && isPointer(events[0], 0, 1, 1) && isPointer(events[1], 0, 4, 4),
          "InputEventQueue coalesces moves into the latest position, after the first event");
  }
  {
    InputEventQueue queue;
    queue.push(pointer(0, 1, 1));
    queue.push(pointer(0, 2, 2));
    queue.push(pointer(1, 3, 3));
    queue.push(pointer(1, 4, 4));
    queue.push(pointer(1, 5, 5));
    queue.push(pointer(0, 6, 6));
    std::deque<ClientEvent> events = queue.take();
    check(events.size() == 5 &&
          isPointer(events[0], 0, 1, 1) && isPointer(events[1], 0, 2, 2) &&
          isPointer(events[2], 1, 3, 3) && isPointer(events[3], 1, 5, 5) &&
          isPointer(events[4], 0, 6, 6),
          "InputEventQueue keeps a press and a release where they happened, and coalesces the drag between");
  }
  {
    InputEventQueue queue;
    queue.push(pointer(0, 1, 1));
    queue.push(pointer(0, 2, 2));
    queue.push(KeyEvent{true, 'a'});
    queue.push(pointer(0, 3, 3));
    std::deque<ClientEvent> events = queue.take();
    check(events.size() == 4 && isPointer(events[1], 0, 2, 2) &&
          std::holds_alternative<KeyEvent>(events[2]) && isPointer(events[3], 0, 3, 3),
          "InputEventQueue does not coalesce a move across a key event");
  }
  {
    InputEventQueue queue;
    queue.push(pointer(0, 1, 1));
    queue.push(pointer(0, 2, 2));
    std::deque<ClientEvent> first = queue.take();
    queue.push(pointer(0, 3, 3));
    queue.push(pointer(0, 4, 4));
    std::deque<ClientEvent> second = queue.take();
    check(first.size() == 2 && isPointer(first[1], 0, 2, 2) &&
          second.size() == 1 && isPointer(second[0], 0, 4, 4),
          "InputEventQueue coalesces moves after take(), but not into the events taken");
  }
  {
    InputEventQueue queue;
    queue.push(pointer(0, 1, 1));
    queue.take();
    queue.push(pointer(1, 2, 2));
    queue.push(pointer(1, 3, 3));
    std::deque<ClientEvent> events = queue.take();
    check(events.size() == 2 && isPointer(events[0], 1, 2, 2) && isPointer(events[1], 1, 3, 3),
          "InputEventQueue tells a press after take() from a move by the mask taken before");
  }
  {
    InputEventQueue queue;
    check(queue.take().empty(), "InputEventQueue starts empty");
  }
}
