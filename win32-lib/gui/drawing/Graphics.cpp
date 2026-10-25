// Copyright (C) 2009,2010,2011,2012 GlavSoft LLC.
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

#include "Graphics.h"

Graphics::Graphics(DeviceContext *dc)
: m_dc(dc)
{
}

Graphics::~Graphics()
{
}

void Graphics::fillRect(int l, int t, int r, int b, const Brush &brush)
{
  RECT rect;

  rect.top = t;
  rect.left = l;
  rect.bottom = b;
  rect.right = r;

  FillRect(m_dc->m_dc, &rect, brush.m_brush);
}

void Graphics::ellipse(int l, int t, int r, int b)
{
  Ellipse(m_dc->m_dc, l, t, r, b);
}

void Graphics::rectangle(int l, int t, int r, int b)
{
  Rectangle(m_dc->m_dc, l, t, r, b);
}
