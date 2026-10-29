#ifndef VIEWER_BUFFER_SIZE_H
#define VIEWER_BUFFER_SIZE_H

#include "rfb/FrameBuffer.h"
#include "util/inttypes.h"
#include <limits.h>

namespace ViewerBufferSize {

// The largest frame buffer accepted by default, in pixels (width * height):
// 16 * 8 Mi, which holds four 8K screens of 7680 x 4320.
const UINT64 DEFAULT_MAX_FRAME_BUFFER_PIXELS = 16 * 8 * 1024 * 1024;

// The largest cursor shape accepted from a server, in each dimension.
const int MAX_CURSOR_SIZE = 1024;

// Whether a frame buffer of width x height pixels of bytesPerPixel bytes,
// its rows aligned to 2^rowAlignmentLog2 bytes, fits in memory as the
// viewer's sizes are counted (INT_MAX bytes), and so do its dimensions
// rounded up to multiples of 4.
inline bool fitsFrameBuffer(int width, int height, int bytesPerPixel,
                            unsigned int rowAlignmentLog2)
{
  if (width < 0 || height < 0 || bytesPerPixel <= 0 ||
      rowAlignmentLog2 > FrameBuffer::MAX_ROW_ALIGNMENT_LOG2) {
    return false;
  }

  const UINT64 alignedWidth = (static_cast<UINT64>(width) + 3) & ~static_cast<UINT64>(3);
  const UINT64 alignedHeight = (static_cast<UINT64>(height) + 3) & ~static_cast<UINT64>(3);
  // No product overflows: width and height are below 2^31, so each
  // factor is below 2^34.
  return alignedWidth * alignedHeight <= INT_MAX / bytesPerPixel &&
         FrameBuffer::bytesPerRowFor(width, bytesPerPixel, rowAlignmentLog2) * height <=
           INT_MAX;
}

inline bool fitsDecoderBuffer(int width, int height, int bytesPerPixel)
{
  if (width < 0 || height < 0 || bytesPerPixel <= 0) {
    return false;
  }

  return static_cast<UINT64>(width) * height <= INT_MAX / bytesPerPixel;
}

} // namespace ViewerBufferSize

#endif // VIEWER_BUFFER_SIZE_H
