// Copyright (C) 2008,2009,2010,2011,2012 GlavSoft LLC.
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

#ifndef __FRAMEBUFFER_H__
#define __FRAMEBUFFER_H__

#include "region/Dimension.h"
#include "rfb/PixelFormat.h"

#include <memory>

/**
 * Functor for @c std::unique_ptr to de-allocate an aligned memory block allocated
 * with <c>operator new</c> with a @c std::align_val_t argument.
 */
class AlignedDeleter final
{
  std::align_val_t m_alignment;
public:
  AlignedDeleter(std::align_val_t alignment = std::align_val_t{ 1 }) noexcept : m_alignment(alignment) {}
  void operator()(void* ptr) noexcept { ::operator delete[](ptr, m_alignment); }
};

// FIXME: Move implementation to the .cpp file.
class FrameBuffer
{
public:
  // The largest row alignment, as a power of 2: 
  // rows at multiples of 256 bytes (= 2048 bits, more than enough for any SIMD)
  static const unsigned int MAX_ROW_ALIGNMENT_LOG2 = 8;

  // The bytes from a pixel to the one below it, in a frame buffer of
  // width pixels of bytesPerPixel bytes with rows aligned to
  // 2^rowAlignmentLog2 bytes: width * bytesPerPixel, rounded up to a
  // multiple of the alignment.
  // Pre-conditions: width and bytesPerPixel are not negative;
  // rowAlignmentLog2 <= MAX_ROW_ALIGNMENT_LOG2.
  static UINT64 bytesPerRowFor(int width, int bytesPerPixel, unsigned int rowAlignmentLog2);

  FrameBuffer(void);
  FrameBuffer(const FrameBuffer &) = delete;
  FrameBuffer &operator=(const FrameBuffer &) = delete;
  FrameBuffer(FrameBuffer &&) = delete;
  FrameBuffer &operator=(FrameBuffer &&) = delete;

  virtual ~FrameBuffer(void);

  virtual bool assignProperties(const FrameBuffer &srcFrameBuffer);
  virtual bool clone(const FrameBuffer &srcFrameBuffer);
  // Fills the whole frame buffer with the given colour, scaled from 0-255
  // to the colour maxima of the pixel format.
  // Pre-conditions: the pixel format is valid (PixelFormat::isValid()).
  // Post-conditions: every pixel holds the colour.
  virtual void setColor(UINT8 red, UINT8 green, UINT8 blue);
  // Fills a rectangle with a pixel value, clipped to the frame buffer.
  // Pre-conditions: dstRect is not 0; color holds a pixel in the frame
  // buffer's format, in its first getBytesPerPixel() bytes in memory.
  // Post-conditions: every pixel of dstRect that lies in the frame buffer
  // holds color; nothing else changes. An empty rectangle, or one wholly
  // outside the frame buffer, changes nothing.
  virtual void fillRect(const Rect &dstRect, UINT32 color);

  // Return value: true - if equal
  //               false - if PixelFormats or size differs
  virtual bool isEqualTo(const FrameBuffer &frameBuffer);

  // Copy to self by specified destination rectangle from the specified
  // coordinates of srcFrameBuffer
  virtual bool copyFrom(const Rect &dstRect, const FrameBuffer &srcFrameBuffer,
                int srcX, int srcY);
  // The same as above but destination rect is m_dimension
  virtual bool copyFrom(const FrameBuffer &srcFrameBuffer,
                int srcX, int srcY);

  virtual void move(const Rect &dstRect, const int srcX, const int srcY);
  // Return value: true - if equal
  //               false - if PixelFormats or data differs
  virtual bool cmpFrom(const Rect &dstRect, const FrameBuffer &srcFrameBuffer,
               const int srcX, const int srcY);

  virtual bool setDimension(const Dimension &newDim);
  virtual bool setDimension(const Rect &rect)
  {
    Dimension dim(rect);
    return setDimension(dim);
  }

  // Sets dimension to the frame buffer without buffer resizing
  virtual void setEmptyDimension(const Rect &dimByRect);

  // Sets pixel format to the frame buffer without buffer resizing
  virtual void setEmptyPixelFmt(const PixelFormat &pf);

  virtual void setPropertiesWithoutResize(const Dimension &newDim, const PixelFormat &pf);

  virtual inline Dimension getDimension() const { return m_dimension; }

  virtual bool setPixelFormat(const PixelFormat &pixelFormat);
  virtual inline PixelFormat getPixelFormat() const { return m_pixelFormat; }

  // This function set both PixelFormat and Dimension
  virtual bool setProperties(const Dimension &newDim, const PixelFormat &pixelFormat);
  virtual bool setProperties(const Rect &dimByRect, const PixelFormat &pixelFormat);

  // Return the number of bits occupied by one pixel (can be 8, 16 or 32).
  virtual UINT8 getBitsPerPixel() const;

  // Return the number of bytes occupied by one pixel (can be 1, 2 or 4).
  virtual UINT8 getBytesPerPixel() const;

  // Uses newBuffer as the pixels, for a frame buffer whose memory belongs
  // to something else (a DIB section).
  // Pre-conditions: newBuffer is 0, or holds getBufferSize() bytes for as
  // long as it is used; it is not owned, and not freed here.
  // Post-conditions: getBuffer() is newBuffer; a buffer that this frame
  // buffer allocated is freed.
  virtual void setBuffer(void *newBuffer)
  {
    m_ownBuffer.reset();
    m_buffer = newBuffer;
  }
  virtual inline void *getBuffer() const { return m_buffer; }

  // Return a pointer to the pixel data specified by the coordinates of that
  // pixel. getBufferPtr(0, 0) should be equivalent to getBuffer(). This
  // function does not check if the coordinates are within the frame buffer
  // boundaries.
  // Post-conditions: getBufferPtr(x, y + 1) is getBytesPerRow() bytes past
  // getBufferPtr(x, y): code that walks down the rows advances by that.
  virtual void *getBufferPtr(int x, int y) const;

  // The size of the buffer: getBytesPerRow() * height.
  size_t getBufferSize() const;
  // The distance in bytes from a pixel to the one below it:
  // bytesPerRowFor() the width, pixel size and row alignment. The bytes
  // past a row's pixels, if any, are part of the buffer but hold nothing
  // in particular: nothing here reads or writes them.
  size_t getBytesPerRow() const;

  // Sets the row alignment: each row starts a multiple of
  // 2^rowAlignmentLog2 bytes from the start of the buffer (0: rows follow
  // one another with no gap). The default is 0. It is this frame
  // buffer's own: assignProperties() and clone() keep it.
  // Pre-conditions: rowAlignmentLog2 <= MAX_ROW_ALIGNMENT_LOG2; the next
  // call is one that allocates the buffer (setProperties(),
  // setDimension(), setPixelFormat(), assignProperties() or clone()), as
  // the buffer's layout changes with it.
  void setRowAlignmentLog2(unsigned int rowAlignmentLog2);
  unsigned int getRowAlignmentLog2() const { return m_rowAlignmentLog2; }

protected:
  // Post-conditions: getBuffer() is a new buffer of getBufferSize() bytes,
  // owned by this frame buffer, with undefined contents; returns true. If
  // allocating it throws, the old buffer is left.
  bool resizeBuffer();
  void clipRect(const Rect &dstRect, const FrameBuffer &srcFrameBuffer,
                const int srcX, const int srcY,
                Rect *dstClippedRect, Rect *srcClippedRect);
  void clipRect(const Rect &dstRect, const Rect &srcBufferRect,
                const int srcX, const int srcY,
                Rect *dstClippedRect, Rect *srcClippedRect);

  Dimension m_dimension;

  PixelFormat m_pixelFormat;
  // See setRowAlignmentLog2().
  unsigned int m_rowAlignmentLog2;
  // The pixels: m_ownBuffer's, or a buffer given to setBuffer().
  void *m_buffer;
  typedef std::unique_ptr<UINT8[], AlignedDeleter> AlignedBufferPtr;
  AlignedBufferPtr m_ownBuffer;
};

#endif // __FRAMEBUFFER_H__
