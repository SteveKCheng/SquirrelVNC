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

#include "ConnectionConfig.h"

#include <crtdbg.h>

#define TEST_FAIL(C,R) if (!C) { R = false; }

ConnectionConfig::ConnectionConfig()
: m_swapMouse(false), m_viewOnly(false),
  m_useFullscreen(false), m_use8BitColor(false), m_preferredEncoding(EncodingDefs::TIGHT),
  m_requestSharedSession(true), m_deiconifyOnRemoteBell(false),
  m_isClipboardEnabled(true),
  m_customCompressionLevel(-1), m_jpegCompressionLevel(6),
  m_fitWindow(false), m_requestShapeUpdates(true),
  m_ignoreShapeUpdates(false), m_scaleNumerator(1), m_scaleDenominator(1),
  m_localCursor(DOT_CURSOR), m_allowedCopyRect(true)
{
}

ConnectionConfig::~ConnectionConfig()
{
}

void ConnectionConfig::allowCopyRect(bool allow)
{
  m_allowedCopyRect = allow;
}

bool ConnectionConfig::isCopyRectAllowed()
{
  return m_allowedCopyRect;
}

void ConnectionConfig::setPreferredEncoding(int encoding)
{
  m_preferredEncoding = static_cast<unsigned char>(encoding);
}

int ConnectionConfig::getPreferredEncoding()
{
  return m_preferredEncoding;
}

void ConnectionConfig::use8BitColor(bool use)
{
  m_use8BitColor = use;
}

bool ConnectionConfig::isUsing8BitColor()
{
  return m_use8BitColor;
}

int ConnectionConfig::clampLevel(int level)
{
  // level in interval [0..9]
  // if compression is disable, then level is -1.
  if (level < -1)
    level = 0;
  if (level > 9)
    level = 9;
  return level;
}

void ConnectionConfig::setCustomCompressionLevel(int level)
{
  level = clampLevel(level);

  m_customCompressionLevel = level;
}

void ConnectionConfig::setJpegCompressionLevel(int level)
{
  level = clampLevel(level);

  m_jpegCompressionLevel = level;
}

int ConnectionConfig::getCustomCompressionLevel()
{
  return m_customCompressionLevel;
}

int ConnectionConfig::getJpegCompressionLevel()
{
  return m_jpegCompressionLevel;
}

bool ConnectionConfig::isCustomCompressionEnabled()
{
  return getCustomCompressionLevel() != -1;
}

bool ConnectionConfig::isJpegCompressionEnabled()
{
  return getJpegCompressionLevel() != -1;
}

void ConnectionConfig::disableCustomCompression()
{
  setCustomCompressionLevel(-1);
}

void ConnectionConfig::disableJpegCompression()
{
  setJpegCompressionLevel(-1);
}

void ConnectionConfig::setViewOnly(bool viewOnly)
{
  m_viewOnly = viewOnly;
}

bool ConnectionConfig::isViewOnly()
{
  return m_viewOnly;
}

void ConnectionConfig::enableClipboard(bool enabled)
{
  m_isClipboardEnabled = enabled;
}

bool ConnectionConfig::isClipboardEnabled()
{
  return m_isClipboardEnabled;
}

void ConnectionConfig::enableFullscreen(bool useFullscreen)
{
  m_useFullscreen = useFullscreen;
}

bool ConnectionConfig::isFullscreenEnabled()
{
  return m_useFullscreen;
}

void ConnectionConfig::deiconifyOnRemoteBell(bool deiconifyFlag)
{
  m_deiconifyOnRemoteBell = deiconifyFlag;
}

bool ConnectionConfig::isDeiconifyOnRemoteBellEnabled()
{
  return m_deiconifyOnRemoteBell;
}

int ConnectionConfig::getScaleNumerator()
{
  return m_scaleNumerator;
}

int ConnectionConfig::getScaleDenominator()
{
  return m_scaleDenominator;
}

void ConnectionConfig::normalizeScale(int *n, int *d)
{
  if ((*n < 1) || (*d < 1)) {
    *n = *d = 1;
  }

  // In floating point, so that this cannot overflow.
  double percent = (*n * 100.0) / *d;
  if (percent > 400) {
    *n = 400;
    *d = 100;
  } else if (percent < 1) {
    // (n * 100) / d would be 0, which the scaling code divides by.
    *n = 1;
    *d = 100;
  } else if (*n > MAX_SCALE_TERM || *d > MAX_SCALE_TERM) {
    // n * 100 could overflow; the viewer scales by whole percents anyway.
    // percent >= 1, so this is at least 1.
    *n = static_cast<int>(percent + 0.5);
    *d = 100;
  }

  int g = gcd(*n, *d);

  *n /= g;
  *d /= g;
}

void ConnectionConfig::setScale(int n, int d)
{
  normalizeScale(&n, &d);

  m_scaleNumerator = n;
  m_scaleDenominator = d;
}

void ConnectionConfig::swapMouse(bool enabled)
{
  m_swapMouse = enabled;
}

bool ConnectionConfig::isMouseSwapEnabled()
{
  return m_swapMouse;
}

void ConnectionConfig::setSharedFlag(bool shared)
{
  m_requestSharedSession = shared;
}

bool ConnectionConfig::getSharedFlag()
{
  return m_requestSharedSession;
}

void ConnectionConfig::fitWindow(bool enabled)
{
  m_fitWindow = enabled;
}

bool ConnectionConfig::isFitWindowEnabled()
{
  return m_fitWindow;
}

void ConnectionConfig::requestShapeUpdates(bool requestFlag)
{
  m_requestShapeUpdates = requestFlag;
}

void ConnectionConfig::ignoreShapeUpdates(bool ignoreFlag)
{
  m_ignoreShapeUpdates = ignoreFlag;
}

bool ConnectionConfig::isRequestingShapeUpdates()
{
  return m_requestShapeUpdates;
}

bool ConnectionConfig::isIgnoringShapeUpdates()
{
  return m_ignoreShapeUpdates;
}

bool ConnectionConfig::isValidLocalCursorShape(int cursorShape)
{
  switch (cursorShape) {
  case NO_CURSOR:
  case DOT_CURSOR:
  case NORMAL_CURSOR:
  case SMALL_CURSOR:
    return true;
  default:
    return false;
  } // switch
}

void ConnectionConfig::setLocalCursorShape(int cursorShape)
{
  if (!isValidLocalCursorShape(cursorShape)) {
    cursorShape = DOT_CURSOR;
    _ASSERT(FALSE);
  }

  m_localCursor = cursorShape;
}

int ConnectionConfig::getLocalCursorShape()
{
  return m_localCursor;
}

bool ConnectionConfig::saveToStorage(SettingsManager &sm) const
{
  bool saveAllOk = true;

  // option "use_encoding_1" have need for compatible with old-vnc files
  TEST_FAIL(sm.setBoolean(_T("use_encoding_1"),   m_allowedCopyRect), saveAllOk);
  TEST_FAIL(sm.setBoolean(_T("copyrect"),         m_allowedCopyRect), saveAllOk);
  TEST_FAIL(sm.setBoolean(_T("viewonly"),         m_viewOnly), saveAllOk);
  TEST_FAIL(sm.setBoolean(_T("fullscreen"),       m_useFullscreen), saveAllOk);
  TEST_FAIL(sm.setBoolean(_T("8bit"),             m_use8BitColor), saveAllOk);
  TEST_FAIL(sm.setBoolean(_T("shared"),           m_requestSharedSession), saveAllOk);
  TEST_FAIL(sm.setBoolean(_T("belldeiconify"),    m_deiconifyOnRemoteBell), saveAllOk);
  TEST_FAIL(sm.setBoolean(_T("disableclipboard"), !m_isClipboardEnabled), saveAllOk);
  TEST_FAIL(sm.setBoolean(_T("swapmouse"),        m_swapMouse), saveAllOk);
  TEST_FAIL(sm.setBoolean(_T("fitwindow"),        m_fitWindow), saveAllOk);
  TEST_FAIL(sm.setBoolean(_T("cursorshape"),      m_requestShapeUpdates), saveAllOk);
  TEST_FAIL(sm.setBoolean(_T("noremotecursor"),   m_ignoreShapeUpdates), saveAllOk);

  TEST_FAIL(sm.setByte(_T("preferred_encoding"),  m_preferredEncoding), saveAllOk);
  TEST_FAIL(sm.setInt(_T("compresslevel"),        m_customCompressionLevel), saveAllOk);
  TEST_FAIL(sm.setInt(_T("quality"),              m_jpegCompressionLevel), saveAllOk);
  TEST_FAIL(sm.setInt(_T("localcursor"),          m_localCursor), saveAllOk);
  TEST_FAIL(sm.setInt(_T("scale_den"),            m_scaleDenominator), saveAllOk);
  TEST_FAIL(sm.setInt(_T("scale_num"),            m_scaleNumerator), saveAllOk);

  TEST_FAIL(sm.setInt(_T("local_cursor_shape"),   m_localCursor), saveAllOk);

  return saveAllOk;
}

bool ConnectionConfig::loadFromStorage(SettingsManager &sm)
{
  bool loadAllOk = true;


  // option "use_encoding_1" have need for compatible with old-vnc files
  TEST_FAIL(sm.getBoolean(_T("use_encoding_1"),   &m_allowedCopyRect), loadAllOk);
  TEST_FAIL(sm.getBoolean(_T("copyrect"),         &m_allowedCopyRect), loadAllOk);
  TEST_FAIL(sm.getBoolean(_T("viewonly"),         &m_viewOnly), loadAllOk);
  TEST_FAIL(sm.getBoolean(_T("fullscreen"),       &m_useFullscreen), loadAllOk);
  TEST_FAIL(sm.getBoolean(_T("8bit"),             &m_use8BitColor), loadAllOk);
  TEST_FAIL(sm.getBoolean(_T("shared"),           &m_requestSharedSession), loadAllOk);
  TEST_FAIL(sm.getBoolean(_T("belldeiconify"),    &m_deiconifyOnRemoteBell), loadAllOk);

  if (sm.getBoolean(_T("disableclipboard"), &m_isClipboardEnabled)) {
    m_isClipboardEnabled = !m_isClipboardEnabled;
  } else {
    loadAllOk = false;
  }

  TEST_FAIL(sm.getBoolean(_T("swapmouse"),        &m_swapMouse), loadAllOk);
  TEST_FAIL(sm.getBoolean(_T("fitwindow"),        &m_fitWindow), loadAllOk);
  TEST_FAIL(sm.getBoolean(_T("cursorshape"),      &m_requestShapeUpdates), loadAllOk);
  TEST_FAIL(sm.getBoolean(_T("noremotecursor"),   &m_ignoreShapeUpdates), loadAllOk);

  TEST_FAIL(sm.getByte(_T("preferred_encoding"),  (char *)&m_preferredEncoding), loadAllOk);

  TEST_FAIL(sm.getInt(_T("compresslevel"),        &m_customCompressionLevel), loadAllOk);
  TEST_FAIL(sm.getInt(_T("quality"),              &m_jpegCompressionLevel), loadAllOk);
  TEST_FAIL(sm.getInt(_T("localcursor"),          &m_localCursor), loadAllOk);
  TEST_FAIL(sm.getInt(_T("scale_den"),            &m_scaleDenominator), loadAllOk);
  TEST_FAIL(sm.getInt(_T("scale_num"),            &m_scaleNumerator), loadAllOk);

  TEST_FAIL(sm.getInt(_T("local_cursor_shape"),   &m_localCursor), loadAllOk);

  // The values above may come from a .vnc file from anywhere: make them
  // valid as the setters do. Without this, scale_den=0 divided by zero.
  m_customCompressionLevel = clampLevel(m_customCompressionLevel);
  m_jpegCompressionLevel = clampLevel(m_jpegCompressionLevel);
  normalizeScale(&m_scaleNumerator, &m_scaleDenominator);
  if (!isValidLocalCursorShape(m_localCursor)) {
    m_localCursor = DOT_CURSOR;
  }

  return loadAllOk;
}

// Greatest common denominator, by Euclid
int ConnectionConfig::gcd(int a, int b)
{
  if (a < b) {
    return gcd(b, a);
  }
  if (b == 0) {
    return a;
  }
  return gcd(b, a % b);
}
