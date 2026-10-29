// Copyright (C) 2012 GlavSoft LLC.
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

#include "DesktopWindow.h"

#include "thread/AutoLock.h"
#include "util/Exception.h"

#include <algorithm>
#include <cstring>
#include <vector>

DesktopWindow::DesktopWindow(LogWriter *logWriter, ConnectionConfig *conConf)
: m_logWriter(logWriter),
  m_isConnected(false),
  m_clipboard(nullptr),
  m_showVert(false),
  m_showHorz(false),
  m_fbWidth(1),
  m_fbHeight(1),
  m_winResize(false),
  m_conConf(conConf),
  m_brush(RGB(0, 0, 0)),
  m_viewerCore(nullptr),
  m_ctrlDown(false),
  m_altDown(false),
  m_previousMousePos(-1, -1),
  m_previousMouseState(0),
  m_isBackgroundDirty(false),
  m_localCursor(LoadCursor(nullptr, IDC_ARROW)),
  m_showRemoteCursor(false),
  m_hasRemoteCursor(false)
{
  // Not make_unique(): RfbKeySymListener is a protected base, so only this
  // class can convert this to it.
  // The cast is here because make_unique cannot reach the private base.
  m_rfbKeySym = std::make_unique<RfbKeySym>(static_cast<RfbKeySymListener *>(this),
                                            m_logWriter);
}

DesktopWindow::~DesktopWindow()
{
}

void DesktopWindow::setConnected()
{
  m_isConnected = true;
}

void DesktopWindow::setViewerCore(RemoteViewerCore *viewerCore)
{
  m_viewerCore = viewerCore;
}

bool DesktopWindow::onCreate(LPCREATESTRUCT /* pcs */)
{
  m_sbar.setWindow(getHWnd());
  m_clipboard.setHWnd(getHWnd());
  if (!AddClipboardFormatListener(getHWnd())) {
    m_logWriter->error(_T("Cannot listen for clipboard changes (error %lu)"),
                       GetLastError());
  }
  return true;
}

void DesktopWindow::onPaint(DeviceContext *dc, PAINTSTRUCT *paintStruct)
{
  Rect paintRect(paintStruct->rcPaint);

  if (paintRect.area() != 0) {
    try {
      AutoLock al(&m_bufferLock);
      m_framebuffer.setTargetDC(paintStruct->hdc);
      if (!m_clientArea.isEmpty()) {
        doDraw(dc);
      }
    } catch (const Exception &ex) {
      m_logWriter->error(_T("Error in onPaint: %s"), ex.getMessage());
    }
  }
}

bool DesktopWindow::onMessage(UINT message, WPARAM wParam, LPARAM lParam)
{
  switch (message) {
    case WM_HSCROLL:
      return onHScroll(wParam, lParam);
    case WM_VSCROLL:
      return onVScroll(wParam, lParam);
    case WM_ERASEBKGND:
      return onEraseBackground(reinterpret_cast<HDC>(wParam));
    case WM_DEADCHAR:
    case WM_SYSDEADCHAR:
      return onDeadChar(wParam, lParam);
    case WM_CLIPBOARDUPDATE:
      return onClipboardUpdate();
    case WM_CREATE:
      return onCreate(reinterpret_cast<LPCREATESTRUCT>(lParam));
    case WM_SIZE:
      return onSize(wParam, lParam);
    case WM_DESTROY:
      return onDestroy();
    case WM_CHAR:
    case WM_SYSCHAR:
      return onChar(wParam, lParam);
    case WM_KEYDOWN:
    case WM_KEYUP:
    case WM_SYSKEYDOWN:
    case WM_SYSKEYUP:
      return onKey(wParam, lParam);
    case WM_SETFOCUS:
      m_rfbKeySym->processFocusRestoration();
      return true;
    case WM_SETCURSOR:
      return onSetCursor(wParam, lParam);
    case WM_KILLFOCUS:
      m_ctrlDown = false;
      m_altDown = false;
      m_rfbKeySym->processFocusLoss();
      return true;
  }
  return false;
}

bool DesktopWindow::onEraseBackground(HDC /* hdc */) {
  return true;
}

bool DesktopWindow::onHScroll(WPARAM wParam, LPARAM /* lParam */) 
{
  switch(LOWORD(wParam)) {
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
      m_sbar.setHorzPos(HIWORD(wParam));
      redraw();
      break;
    case SB_LINELEFT:
      m_sbar.moveLeftHorz(ScrollBar::SCROLL_STEP);
      redraw();
      break;
    case SB_PAGELEFT:   
      m_sbar.moveLeftHorz();
      redraw();
      break;
    case SB_LINERIGHT:
      m_sbar.moveRightHorz(ScrollBar::SCROLL_STEP);
      redraw();
      break;
    case SB_PAGERIGHT:    
      m_sbar.moveRightHorz();
      redraw();
      break;
  }
  return true;
}

bool DesktopWindow::onVScroll(WPARAM wParam, LPARAM /* lParam */) 
{
  switch(LOWORD(wParam)) {
    case SB_THUMBTRACK:
    case SB_THUMBPOSITION:
      m_sbar.setVertPos(HIWORD(wParam));
      redraw();
      break;
    case SB_LINEUP:
      m_sbar.moveDownVert(ScrollBar::SCROLL_STEP);
      redraw();
      break;
    case SB_PAGEUP:
      m_sbar.moveDownVert();
      redraw();
      break;
    case SB_LINEDOWN:
      m_sbar.moveUpVert(ScrollBar::SCROLL_STEP);
      redraw();
      break;
    case SB_PAGEDOWN:
      m_sbar.moveUpVert();
      redraw();
      break;
  }
  return true;
}

bool DesktopWindow::onMouse(unsigned char mouseButtons, unsigned short wheelSpeed, POINT position)
{
  // If mode is "view-only", then skip event.
  if (m_conConf->isViewOnly()) {
    return true;
  }

  // If viewer isn't connected with server, then skip event.
  if (!m_isConnected) {
    return true;
  }

  // If swap of mouse button is enabled, then swap button.
  if (m_conConf->isMouseSwapEnabled() && mouseButtons) {
    bool bSecond = !!(mouseButtons & MOUSE_MDOWN);
    bool bThird  = !!(mouseButtons & MOUSE_RDOWN);
    mouseButtons &= ~(MOUSE_MDOWN | MOUSE_RDOWN);
    if (bSecond) {  
      mouseButtons |= MOUSE_RDOWN;
    }
    if (bThird) {
      mouseButtons |= MOUSE_MDOWN;
    }
  }

  // Translate coordinates from the Viewer Window to Desktop Window.
  POINTS mousePos = getViewerCoord(position.x, position.y);
  Point pos;
  pos.x = mousePos.x;
  pos.y = mousePos.y;

  // If coordinats of point is invalid, then skip event.
  if (pos.x >= 0 && pos.y >= 0) {
    UINT8 buttons = mouseButtons;

    // If posititon of mouse isn't change, then don't send event to server.
    if (buttons != m_previousMouseState || !pos.isEqualTo(m_previousMousePos)) {
      int wheelMask = MOUSE_WUP | MOUSE_WDOWN;

      // Update previously position of mouse.
      m_previousMouseState = buttons & ~wheelMask;
      m_previousMousePos = pos;

      if ((buttons & wheelMask) == 0) {
        // Send position of cursor and state of buttons one time.
        sendPointerEvent(buttons, pos);
      } else {
        // Send position of cursor and state of buttons wheelSpeed times.
        for (int i = 0; i < wheelSpeed; i++) {
          sendPointerEvent(buttons, pos);
          sendPointerEvent(buttons & ~wheelMask, pos);
        }
      } // wheel
    }
  }
  return true;
}

bool DesktopWindow::onKey(WPARAM wParam, LPARAM lParam) 
{
  if (!m_conConf->isViewOnly()) {
    unsigned short virtualKey = static_cast<unsigned short>(wParam);
    unsigned int additionalInfo = static_cast<unsigned int>(lParam);
    static const unsigned int DOWN_FLAG = 0x80000000;
    bool isDown = (additionalInfo & DOWN_FLAG) == 0;

    if (virtualKey == VK_PROCESSKEY) {
      bool extended = HIWORD(lParam) & KF_EXTENDED;
      UINT scancode = HIWORD(lParam) & 0xFF;
      if (extended) {
        scancode += 0xE000;
      }
      // Virtual-key codes are 8 bits.
      virtualKey = static_cast<unsigned short>(MapVirtualKey(scancode, MAPVK_VSC_TO_VK_EX));
    }

    if (virtualKey == VK_CONTROL) {
      m_ctrlDown = isDown;
    }

    if (virtualKey == VK_MENU) {
      m_altDown = isDown;
    }

    m_rfbKeySym->processKeyEvent(virtualKey,
                                 additionalInfo);
  }
  return true;
}

bool DesktopWindow::onChar(WPARAM wParam, LPARAM lParam) 
{
  if (!m_conConf->isViewOnly()) {
    m_rfbKeySym->processCharEvent(static_cast<WCHAR>(wParam), 
                                  static_cast<unsigned int>(lParam));
  }
  return true;
}

bool DesktopWindow::onDeadChar(WPARAM /* wParam */, LPARAM /* lParam */) 
{
  return true;
}

bool DesktopWindow::onClipboardUpdate()
{
  if (!IsWindowVisible(getHWnd()) || !m_conConf->isClipboardEnabled()) {
    return false;
  }
  // The server's text, put there by setClipboardData(): not sent back.
  if (m_clipboard.isUnchangedSinceSetString()) {
    return true;
  }
  StringStorage clipboardString;
  if (m_clipboard.getString(&clipboardString)) {
    sendCutTextEvent(clipboardString);
  }
  return true;
}

void DesktopWindow::setClipboardData(const StringStorage &strText)
{
  if (m_conConf->isClipboardEnabled()) {
    m_clipboard.setString(strText);
  }
}

void DesktopWindow::doDraw(DeviceContext *dc)
{
  AutoLock al(&m_bufferLock);
  int fbWidth  = m_framebuffer.getDimension().width;
  int fbHeight = m_framebuffer.getDimension().height;

  if (!fbWidth || !fbHeight) {
    Graphics graphics(dc);

    graphics.fillRect(m_clientArea.left, m_clientArea.top, 
                      m_clientArea.right, m_clientArea.bottom, m_brush);
    return;
  }

  scrollProcessing(fbWidth, fbHeight);

  int iHorzPos = 0; 
  int iVertPos = 0; 

  if (m_showHorz) { 
    iHorzPos = m_sbar.getHorzPos();
  }

  if (m_showVert) {
    iVertPos = m_sbar.getVertPos();
  }

  m_scManager.setStartPoint(iHorzPos, iVertPos);

  Rect src, dst;
  m_scManager.getSourceRect(&src);
  m_scManager.getDestinationRect(&dst);

  int iWidth = m_clientArea.getWidth() - dst.getWidth();
  int iHeight = m_clientArea.getHeight() - dst.getHeight();

  RECT rcSrc = src.toWindowsRect();
  RECT rcDst = dst.toWindowsRect();
  if (iWidth || iHeight) {
    RECT rcClient = m_clientArea.toWindowsRect();
    drawBackground(dc, rcClient, rcDst);
  }

  drawImage(rcSrc, rcDst);
}

void DesktopWindow::applyScrollbarChanges(bool isChanged, bool isVert, bool isHorz, int wndWidth, int wndHeight)
{
  if (m_showVert != isVert) {
    m_showVert = isVert;
    m_sbar.showVertScroll(m_showVert);
    isChanged = true;
  }

  if (m_showHorz != isHorz) {
    m_showHorz = isHorz;
    m_sbar.showHorzScroll(m_showHorz);
    isChanged = true;
  }

  if (isChanged) {
    Rect rcWindow(0, 0, wndWidth, wndHeight);
    m_scManager.setWindow(&rcWindow);
    if (m_showVert) {
      m_sbar.setVertRange(0, m_scManager.getVertPoints(), wndHeight);
    }
    if (m_showHorz) {
      m_sbar.setHorzRange(0, m_scManager.getHorzPoints(), wndWidth);
    }
    calcClientArea();
  }
}

void DesktopWindow::calculateWndSize(bool isChanged)
{
  long hScroll = 0;
  long vScroll = 0;
  if (m_showVert) {
    vScroll = m_sbar.getVerticalSize();
  }
  if (m_showHorz) {
    hScroll = m_sbar.getHorizontalSize();
  }

  int wndWidth  = vScroll + m_clientArea.getWidth();
  int wndHeight = hScroll + m_clientArea.getHeight();
  bool isVert   = m_scManager.getVertPages(wndHeight);
  bool isHorz   = m_scManager.getHorzPages(wndWidth);

  if (isHorz) {
    wndHeight -= m_sbar.getHorizontalSize();
  }
  if (isVert) {
    wndWidth -= m_sbar.getVerticalSize();
  }

  if (isVert != isHorz) {
     // need to recalculate bHorz and bVert
    isVert = m_scManager.getVertPages(wndHeight);
    isHorz = m_scManager.getHorzPages(wndWidth);
    if (isHorz) {
      wndHeight -= m_sbar.getHorizontalSize();
    }
    if (isVert) {
      wndWidth -= m_sbar.getVerticalSize();
    }
  }
  applyScrollbarChanges(isChanged, isVert, isHorz, wndWidth, wndHeight);
}

void DesktopWindow::scrollProcessing(int fbWidth, int fbHeight)
{
  bool bChanged = false;

  if (m_winResize) {
    m_winResize = false;
    bChanged = true;
  }

  if (fbWidth != m_fbWidth || fbHeight != m_fbHeight) {
    bChanged = true;
    m_fbWidth = fbWidth;
    m_fbHeight = fbHeight;
    m_scManager.setScreenResolution(fbWidth, fbHeight);
  }

  calculateWndSize(bChanged);
}

void DesktopWindow::drawBackground(DeviceContext *dc, const RECT &rcMain, const RECT &rcImage) {
  Graphics graphics(dc);

  // top rectangle
  graphics.fillRect(rcMain.left, rcMain.top, rcMain.right, rcImage.top, m_brush);
  // left rectangle
  graphics.fillRect(rcMain.left, rcImage.top, rcImage.left, rcImage.bottom, m_brush);
  // bottom rectangle
  graphics.fillRect(rcMain.left, rcImage.bottom, rcMain.right, rcMain.bottom, m_brush);
  // right rectangle
  graphics.fillRect(rcImage.right, rcImage.top, rcMain.right, rcImage.bottom, m_brush);
}

void DesktopWindow::calcClientArea() {
  if (getHWnd()) {
    RECT rc;

    getClientRect(&rc);
    m_clientArea.fromWindowsRect(rc);
    m_scManager.setWindow(&m_clientArea);
  }
}

void DesktopWindow::drawImage(const RECT &src, const RECT &dst)
{
  Rect rc_src(src);
  Rect rc_dest(dst);

  AutoLock al(&m_bufferLock);
  
  if ((src.right - src.left) == (dst.right - dst.left) &&
     (src.bottom - src.top) == (dst.bottom - dst.top) &&
     src.left == dst.left &&
     src.right == dst.right &&
     src.top == dst.top &&
     src.bottom == dst.bottom) {
    m_framebuffer.blitFromDibSection(rc_dest);
  } else {
    m_framebuffer.stretchFromDibSection(rc_dest, rc_src);
  }
}

bool DesktopWindow::onSize(WPARAM /* wParam */, LPARAM /* lParam */) 
{
  calcClientArea();
  m_winResize = true; 
  return true;
}

bool DesktopWindow::onDestroy()
{
  RemoveClipboardFormatListener(getHWnd());
  return true;
}

void DesktopWindow::updateFramebuffer(const FrameBuffer &framebuffer,
                                     const Rect &dstRect)
{
  // This code doesn't require blocking of m_framebuffer.
  //
  // If in this moment Windows paint frame buffer to screen,
  // then image on viewer is not valid, but next update fix this
  // It is not critical.
  //
  // Size of framebuffer can not changed, because onFrameBufferUpdate()
  // and onFrameBufferPropChange() may be called only from one thread.

  if (!m_framebuffer.copyFrom(dstRect, framebuffer, dstRect.left, dstRect.top)) {
    m_logWriter->error(_T("Possible invalide region. (%d, %d), (%d, %d)"),
                       dstRect.left, dstRect.top, dstRect.right, dstRect.bottom);
    m_logWriter->interror(_T("Error in updateFramebuffer (ViewerWindow)"));
  }
  repaint(dstRect);
}

void DesktopWindow::setNewFramebuffer(const FrameBuffer &framebuffer)
{
  Dimension dimension = framebuffer.getDimension();
  Dimension olddimension = m_framebuffer.getDimension();

  bool isBackgroundDirty = dimension.width < olddimension.width || 
                           dimension.height < olddimension.height;

  m_logWriter->detail(_T("Desktop size: %d, %d"),  dimension.width, dimension.height);
  {
    // FIXME: Nested locks should not be used.
    AutoLock al(&m_bufferLock);

    m_serverDimension = dimension;
    if (!dimension.isEmpty()) {
      // the width and height should be aligned to 4
      int alignWidth = (dimension.width + 3) / 4;
      dimension.width = alignWidth * 4;
      int alignHeight = (dimension.height + 3) / 4;
      dimension.height = alignHeight * 4;
      PixelFormat pixelFormat = framebuffer.getPixelFormat();
      m_framebuffer.setProperties(dimension, pixelFormat, getHWnd());
      m_framebuffer.setColor(0, 0, 0);
      m_scManager.setScreenResolution(dimension.width, dimension.height);
    } else {
      // If size of remote frame buffer is (0, 0), then fill viewer.
      // Otherwise user might think that everything hangs.
      m_framebuffer.setColor(0, 0, 0);
    }
  }
  if (dimension.isEmpty()) {
    Rect rect = dimension.getRect();
    repaint(rect);
  } else {
    m_isBackgroundDirty = isBackgroundDirty;
  }
}

void DesktopWindow::repaint(const Rect &repaintRect)
{
  Rect rect;
  m_scManager.getSourceRect(&rect);
  Rect paint = repaintRect;
  paint = paint.intersection(rect);
  if (paint.isEmpty() && !m_isBackgroundDirty) {
    // None of it is visible.
    return;
  }

  // checks what we getted a valid rectangle
  if (paint.getWidth() <= 1 || paint.getHeight() <= 1 || m_isBackgroundDirty) {
    m_isBackgroundDirty = false;
    redraw();
    return;
  }
  Rect wnd;
  m_scManager.getWndFromScreen(paint, &wnd);
  m_scManager.getDestinationRect(&rect);
  if (wnd.left) {
    --wnd.left;
  }
  if (wnd.top) {
    --wnd.top;
  }
  if (wnd.right < rect.right) {
    ++wnd.right;
  }
  if (wnd.bottom < rect.bottom) {
    ++wnd.bottom;
  }
  wnd = wnd.intersection(rect);
  RECT rcWnd = wnd.toWindowsRect();
  redraw(&rcWnd);
}

void DesktopWindow::setScale(int scale)
{
  AutoLock al(&m_bufferLock);
  m_scManager.setScale(scale);
  m_winResize = true;
  // Invalidate all area of desktop window.
  if (m_hWnd != nullptr) {
    redraw();
  }
}

POINTS DesktopWindow::getViewerCoord(long xPos, long yPos)
{
  Rect rect;
  POINTS p;

  m_scManager.getDestinationRect(&rect);
  // it checks this point in the rect
  if (!rect.isPointInRect(xPos, yPos)) {
    p.x = -1;
    p.y = -1;
    return p;
  }
  p = m_scManager.transformDispToScr(xPos, yPos);
  return p;
}

Rect DesktopWindow::getViewerGeometry()
{
  Rect viewerRect;
  viewerRect.setHeight(m_scManager.getVertPoints());
  viewerRect.setWidth(m_scManager.getHorzPoints());
  if (viewerRect.area() == 0) {
    viewerRect.setWidth(m_framebuffer.getDimension().width);
    viewerRect.setHeight(m_framebuffer.getDimension().height);
  }
  return viewerRect;
}

Rect DesktopWindow::getFrameBufferGeometry()
{
  AutoLock al(&m_bufferLock);
  return m_framebuffer.getDimension().getRect();
}

void DesktopWindow::getServerGeometry(Rect *rect, int *pixelsize)
{
  AutoLock al(&m_bufferLock);
  if (rect != nullptr) {
    *rect = m_serverDimension.getRect();
  }
  if (pixelsize != nullptr) {
    *pixelsize = m_framebuffer.getBitsPerPixel();
  }
}

void DesktopWindow::onRfbKeySymEvent(unsigned int rfbKeySym, bool down)
{
  sendKeyboardEvent(down, rfbKeySym);
}

void DesktopWindow::setCtrlState(const bool ctrlState)
{
  m_ctrlDown = ctrlState;
}

void DesktopWindow::setAltState(const bool altState)
{
  m_altDown = altState;
}

bool DesktopWindow::getCtrlState() const
{
  return m_ctrlDown;
}

bool DesktopWindow::getAltState() const
{
  return m_altDown;
}

void DesktopWindow::sendKey(WCHAR key, bool pressed)
{
  m_rfbKeySym->sendModifier(static_cast<unsigned char>(key), pressed);
}

void DesktopWindow::sendCtrlAltDel()
{
  m_rfbKeySym->sendCtrlAltDel();
}

void DesktopWindow::sendKeyboardEvent(bool downFlag, UINT32 key)
{
  if (m_conConf->isViewOnly()) {
    return;
  }

  // If pointer to viewer core is 0, then exit.
  if (m_viewerCore == nullptr) {
    return;
  }

  // Trying send keyboard event...
  try {
    m_viewerCore->sendKeyboardEvent(downFlag, key);
  } catch (const Exception &exception) {
    m_logWriter->detail(_T("Error in DesktopWindow::sendKeyboardEvent(): %s"),
                        exception.getMessage());
  }
}

void DesktopWindow::sendPointerEvent(UINT8 buttonMask, const Point &position)
{
  if (m_conConf->isViewOnly()) {
    return;
  }

  // If pointer to viewer core is 0, then exit.
  if (m_viewerCore == nullptr) {
    return;
  }

  // Trying send pointer event...
  try {
    m_viewerCore->sendPointerEvent(buttonMask, position);
  } catch (const Exception &exception) {
    m_logWriter->detail(_T("Error in DesktopWindow::sendPointerEvent(): %s"),
                        exception.getMessage());
  }
}

void DesktopWindow::sendCutTextEvent(const StringStorage &cutText)
{
  if (!m_conConf->isClipboardEnabled()) {
    return;
  }

  // If pointer to viewer core is 0, then exit.
  if (m_viewerCore == nullptr) {
    return;
  }

  // Trying send cut-text event...
  try {
    m_viewerCore->sendCutTextEvent(cutText);
  } catch (const Exception &exception) {
    m_logWriter->detail(_T("Error in DesktopWindow::sendCutTextEvent(): %s"),
                        exception.getMessage());
  }
}

//
// A Windows cursor of image, or null if it cannot be made.
//
// Pre-conditions: image has pixels.
// Post-conditions: the caller owns the result (DestroyIcon()).
//
static HCURSOR createCursor(const CursorImage &image)
{
  BITMAPV5HEADER header = {};
  header.bV5Size = sizeof(header);
  header.bV5Width = image.width;
  // Top row first, as the image is.
  header.bV5Height = -image.height;
  header.bV5Planes = 1;
  header.bV5BitCount = 32;
  header.bV5Compression = BI_BITFIELDS;
  header.bV5RedMask = 0x00FF0000;
  header.bV5GreenMask = 0x0000FF00;
  header.bV5BlueMask = 0x000000FF;
  header.bV5AlphaMask = 0xFF000000;

  void *bits = nullptr;
  HDC screen = GetDC(nullptr);
  HBITMAP colour = CreateDIBSection(screen, reinterpret_cast<BITMAPINFO *>(&header),
                                    DIB_RGB_COLORS, &bits, nullptr, 0);
  ReleaseDC(nullptr, screen);
  if (colour == nullptr) {
    return nullptr;
  }
  memcpy(bits, image.pixels.data(), image.pixels.size() * sizeof(UINT32));

  // The AND mask, 1 where transparent. Windows uses the alpha of the
  // colour bitmap, but falls back on the mask when every alpha is 0. Rows
  // of whole 16-bit words, as CreateBitmap() takes them.
  const size_t maskBytesPerRow = (image.width + 15) / 16 * 2;
  std::vector<BYTE> mask(maskBytesPerRow * image.height, 0);
  for (int y = 0; y < image.height; y++) {
    for (int x = 0; x < image.width; x++) {
      if ((image.pixels[static_cast<size_t>(y) * image.width + x] >> 24) == 0) {
        mask[y * maskBytesPerRow + x / 8] |= static_cast<BYTE>(0x80 >> (x % 8));
      }
    }
  }
  HBITMAP maskBitmap = CreateBitmap(image.width, image.height, 1, 1, mask.data());

  HCURSOR cursor = nullptr;
  if (maskBitmap != nullptr) {
    ICONINFO info = {};
    info.fIcon = FALSE;
    // From the server, so it may lie outside the shape.
    info.xHotspot = static_cast<DWORD>(std::clamp(image.hotSpot.x, 0, image.width - 1));
    info.yHotspot = static_cast<DWORD>(std::clamp(image.hotSpot.y, 0, image.height - 1));
    info.hbmMask = maskBitmap;
    info.hbmColor = colour;
    // Copies the bitmaps.
    cursor = CreateIconIndirect(&info);
    DeleteObject(maskBitmap);
  }
  DeleteObject(colour);
  return cursor;
}

void DesktopWindow::setLocalCursor(HCURSOR localCursor)
{
  m_localCursor = localCursor;
  updateCursor();
}

void DesktopWindow::setShowRemoteCursor(bool show)
{
  m_showRemoteCursor = show;
  updateCursor();
}

void DesktopWindow::setRemoteCursor(const CursorImage &cursor)
{
  CursorHandle newCursor;
  bool hasCursor = true;
  if (!cursor.pixels.empty()) {
    newCursor.reset(createCursor(cursor));
    if (!newCursor) {
      m_logWriter->error(_T("Cannot make a cursor of the server's %dx%d shape"),
                         cursor.width, cursor.height);
      hasCursor = false;
    }
  }
  // The old one is destroyed only after the new one is shown.
  CursorHandle oldCursor = std::move(m_remoteCursor);
  m_remoteCursor = std::move(newCursor);
  m_hasRemoteCursor = hasCursor;
  updateCursor();
}

HCURSOR DesktopWindow::getCursor() const
{
  if (m_showRemoteCursor && m_hasRemoteCursor) {
    return m_remoteCursor.get();
  }
  return m_localCursor;
}

void DesktopWindow::updateCursor()
{
  HWND window = getHWnd();
  POINT point;
  if (window == nullptr || !GetCursorPos(&point) || WindowFromPoint(point) != window) {
    return;
  }
  LRESULT hit = SendMessage(window, WM_NCHITTEST, 0, MAKELPARAM(point.x, point.y));
  if (hit == HTCLIENT) {
    SetCursor(getCursor());
  }
}

bool DesktopWindow::onSetCursor(WPARAM wParam, LPARAM lParam)
{
  if (reinterpret_cast<HWND>(wParam) != getHWnd() || LOWORD(lParam) != HTCLIENT) {
    return false;
  }
  SetCursor(getCursor());
  return true;
}
