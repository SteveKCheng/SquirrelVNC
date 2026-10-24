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

#ifndef __DIBFRAMEBUFFER_H__
#define __DIBFRAMEBUFFER_H__

#include "rfb/FrameBuffer.h"
#include "win-system/DibSection.h"

#include <memory>

// This class is a wrapper for a FramBuffer and a DIB section.
// It changes DIB section proerties by oneself according to FrameBuffer
// properties (such as width, height and PixelFormat)
class DibFrameBuffer : public FrameBuffer
{
public:
  DibFrameBuffer();
  ~DibFrameBuffer() override;

  void setColor(UINT8 reg, UINT8 green, UINT8 blue) override;
  void fillRect(const Rect *dstRect, UINT32 color) override;

  bool isEqualTo(const FrameBuffer *frameBuffer) override;

  bool copyFrom(const Rect *dstRect, const FrameBuffer *srcFrameBuffer,
                int srcX, int srcY) override;
  bool copyFrom(const FrameBuffer *srcFrameBuffer,
                int srcX, int srcY) override;
  bool overlay(const Rect *dstRect, const FrameBuffer *srcFrameBuffer,
               int srcX, int srcY, const char *andMask) override;
  void move(const Rect *dstRect, const int srcX, const int srcY) override;
  bool cmpFrom(const Rect *dstRect, const FrameBuffer *srcFrameBuffer,
               const int srcX, const int srcY) override;

  inline Dimension getDimension() const override;

  inline PixelFormat getPixelFormat() const override;

  // This function must uses instead of function that can change the FrameBuffer properties
  // compatibleWindow - is hwnd of a window that will be used to create a compatible DC for
  // the DIB section. Also, a DC of this window will be used as default for the
  // blitting operations. The window or DC for blitting operations can be changed many times during
  // a session of the DIB section later.
  // The compatibleWindow handle can be zero then the function will take a DC of entire desktop.
  // Note that other function that can change properties will throw Exception().
  virtual void setProperties(const Dimension *newDim,
    const PixelFormat *pixelFormat, HWND compatibleWindow);

  // This function changes the target DC. In default target DC is a DC that has been
  // got from a compatible window on object creation. This function can be call many times.
  void setTargetDC(HDC targetDC);

  UINT8 getBitsPerPixel() const override;

  UINT8 getBytesPerPixel() const override;

  inline void *getBuffer() const override;

  void *getBufferPtr(int x, int y) const override;

  inline int getBufferSize() const override;
  inline int getBytesPerRow() const override;

  // This function copies a block of bits from a source DC (that has been used to create the
  // DIB section) to the DIB section.
  // Note that this function does not copy any transparent windows.
  // This function throwing an exception on a failure.
  void blitToDibSection(const Rect *rect);

  // This function copies a block of bits from a source DC (that has been used to create the
  // DIB section) to the DIB section.
  // Note that this function copies transparent windows too.
  // This function throwing an exception on a failure.
  void blitTransparentToDibSection(const Rect *rect);

  // This function copies a block of bits from the DIB section to the source DC
  // (that has been used to create the compatible DIB section).
  // Note that this function does not copy any transparent windows.
  // This function throwing an exception on a failure.
  void blitFromDibSection(const Rect *rect);

  // This function copies with strech a block of bits from the DIB section to the source DC
  // (that has been used to create the compatible DIB section).
  // Note that this function does not copy any transparent windows.
  // This function throwing an exception on a failure.
  void stretchFromDibSection(const Rect *srcRect, const Rect *dstRect);

private:
  // This section to reduce access to some function that have been inherited from the
  // FrameBuffer class and can't to be use in here. Also, if user code will to try
  // use this functions from a base class its will throw Exception.
  bool assignProperties(const FrameBuffer *srcFrameBuffer) override;
  bool clone(const FrameBuffer *srcFrameBuffer) override;
  bool setDimension(const Dimension *newDim) override;
  bool setDimension(const Rect *rect) override;
  void setEmptyDimension(const Rect *dimByRect) override;
  bool setPixelFormat(const PixelFormat *pixelFormat) override;
  void setEmptyPixelFmt(const PixelFormat *pf) override;
  bool setProperties(const Dimension *newDim, const PixelFormat *pixelFormat) override;
  bool setProperties(const Rect *dimByRect, const PixelFormat *pixelFormat) override;
  void setPropertiesWithoutResize(const Dimension *newDim, const PixelFormat *pf) override;
  void setBuffer(void *newBuffer) override;

private:
  // This function updates a DIB section in accord with the FrameBuffer
  void *updateDibSection(const Dimension *newDim,
    const PixelFormat *pixelFormat,
    HWND compatibleWindow);
  void releaseDibSection();

  // This function generates an Exception if DIB section is not initialized yet.
  void checkDibValid();

  FrameBuffer m_fb;
  std::unique_ptr<DibSection> m_dibSection;
};

#endif // __DIBFRAMEBUFFER_H__
