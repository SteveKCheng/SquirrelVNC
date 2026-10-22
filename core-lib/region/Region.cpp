// Copyright (C) 2010,2011,2012 GlavSoft LLC.
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

#include "Region.h"

#include <algorithm>

// The coordinate range of the X region code (MINSHORT to MAXSHORT in
// x11region.h). The macros are not used here: <windows.h> defines macros
// of the same names first, with MINSHORT as 0x8000, which is the int
// 32768, so x11region.h keeps those in C++ code.
static const int MIN_BOX_COORDINATE = -32767;
static const int MAX_BOX_COORDINATE = 32767;

// Converts a coordinate to the 16-bit type of BoxRec, clamping it to the
// range the region code supports.
// Post-conditions: the result is value if it is in the range, or else the
// nearer end of the range.
static short toBoxCoordinate(int value)
{
  return static_cast<short>(std::clamp(value, MIN_BOX_COORDINATE, MAX_BOX_COORDINATE));
}

Region::Region()
{
  miRegionInit(&m_reg, NullBox, 0);
}

// FIXME: Make BoxRec and Rect identical to get rid of conversions.
Region::Region(const Rect &rect)
{
  BoxRec box;
  box.x1 = toBoxCoordinate(rect.left);
  box.x2 = toBoxCoordinate(rect.right);
  box.y1 = toBoxCoordinate(rect.top);
  box.y2 = toBoxCoordinate(rect.bottom);
  // A rectangle wholly outside the range is empty once clamped.
  if (box.x1 < box.x2 && box.y1 < box.y2) {
    miRegionInit(&m_reg, &box, 0);
  } else {
    miRegionInit(&m_reg, NullBox, 0);
  }
}

Region::Region(const Region &src)
{
  miRegionInit(&m_reg, NullBox, 0);
  set(&src);
}

Region::~Region()
{
  miRegionUninit(&m_reg);
}

void Region::clear()
{
  miRegionEmpty(&m_reg);
}

void Region::set(const Region *src)
{
  miRegionCopy(&m_reg, (RegionPtr)&src->m_reg);
}

Region & Region::operator=(const Region &src)
{
  set(&src);
  return *this;
}

void Region::addRect(const Rect *rect)
{
  if (!rect->isEmpty()) {
    Region temp(rect);
    add(&temp);
  }
}

void Region::translate(int dx, int dy)
{
  miTranslateRegion(&m_reg, dx, dy);
}

void Region::add(const Region *other)
{
  miUnion(&m_reg, &m_reg, (RegionPtr)&other->m_reg);
}

void Region::add(const Region &other)
{
  miUnion(&m_reg, &m_reg, (RegionPtr)&other.m_reg);
}

void Region::subtract(const Region *other)
{
  miSubtract(&m_reg, &m_reg, (RegionPtr)&other->m_reg);
}

void Region::intersect(const Region *other)
{
  miIntersect(&m_reg, &m_reg, (RegionPtr)&other->m_reg);
}

void Region::crop(const Rect *rect)
{
  Region temp(rect);
  intersect(&temp);
}

bool Region::isEmpty() const
{
  return (miRegionNotEmpty((RegionPtr)&m_reg) == FALSE);
}

bool Region::isPointInside(int x, int y) const
{
  BoxRec stubBox; // Ignore returning rect.
  return !!miPointInRegion((RegionPtr)&m_reg, x, y, &stubBox);
}

bool Region::equals(const Region *other) const
{
  // Handle a special case when both regions are empty.
  // Such regions may be considered different by miRegionsEqual().
  if (this->isEmpty() && other->isEmpty()) {
    return true;
  }

  return (miRegionsEqual((RegionPtr)&m_reg,
                         (RegionPtr)&other->m_reg) == TRUE);
}

// FIXME: Optimize, make BoxRec and Rect identical to get rid of conversions.
void Region::getRectVector(std::vector<Rect> *dst) const
{
  dst->clear();

  const BoxRec *boxPtr = REGION_RECTS(&m_reg);
  long numRects = REGION_NUM_RECTS(&m_reg);
  dst->reserve((size_t)numRects);
  for (long i = 0; i < numRects; i++) {
    Rect rect(boxPtr[i].x1, boxPtr[i].y1, boxPtr[i].x2, boxPtr[i].y2);
    dst->push_back(rect);
  }
}

// FIXME: Optimize, make BoxRec and Rect identical to get rid of conversions.
void Region::getRectList(std::list<Rect> *dst) const
{
  dst->clear();

  const BoxRec *boxPtr = REGION_RECTS(&m_reg);
  long numRects = REGION_NUM_RECTS(&m_reg);
  for (long i = 0; i < numRects; i++) {
    Rect rect(boxPtr[i].x1, boxPtr[i].y1, boxPtr[i].x2, boxPtr[i].y2);
    dst->push_back(rect);
  }
}

size_t Region::getCount() const
{
  return REGION_NUM_RECTS(&m_reg);
}

Rect Region::getBounds() const
{
  const BoxRec *boxPtr = REGION_EXTENTS(&m_reg);
  return Rect(boxPtr->x1, boxPtr->y1, boxPtr->x2, boxPtr->y2);
}
