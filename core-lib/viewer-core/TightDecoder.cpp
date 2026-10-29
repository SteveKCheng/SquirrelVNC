// Copyright (C) 2011,2012 GlavSoft LLC.
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

#include "TightDecoder.h"
#include "ViewerBufferSize.h"

#include "rfb/StandardPixelFormatFactory.h"

TightDecoder::TightDecoder(LogWriter *logWriter)
: DecoderOfRectangle(logWriter, EncodingDefs::TIGHT),
  m_isCPixel(false)
{

  for (int i = 0; i < DECODERS_NUM; i++) {
    m_inflater.push_back(std::make_unique<Inflater>());
  }
  reset();
}

TightDecoder::~TightDecoder()
{
}

Task<> TightDecoder::decode(AwaitableReader &reader,
                            FrameBuffer &fb,
                            const Rect &dstRect,
                            LocalMutex &fbLock)
{
 // The width of any Tight-encoded rectangle cannot exceed 2048
 // pixels. If a rectangle is wider, it must be split into several rectangles
 // and each one should be encoded separately.

  m_isCPixel = false;
  PixelFormat pf = fb.getPixelFormat();
  if (pf.colorDepth == 24 && pf.bitsPerPixel == 32 &&
      pf.redMax == 255 && pf.greenMax == 255 && pf.blueMax == 255) {
    m_isCPixel = true;
}

  UINT8 compressionControl = co_await reader.readUInt8();
  resetDecoders(compressionControl);
  UINT8 compressionType = (compressionControl >> 4) & 0x0F;

  if (compressionType > MAX_SUBENCODING) {
    throw Exception(_T("Sub-encoding in Tight-encoder are not valid"));
  }
  
  if (!fb.getDimension().getRect().intersection(dstRect).isEqualTo(dstRect))
    throw Exception(_T("Error in protocol: incorrect size of rectangle (tight-decoder)"));

  if (compressionType == FILL_TYPE) {
    UINT32 color = co_await readTightPixel(reader, pf);
    AutoLock al(&fbLock);
    fb.fillRect(dstRect, color);
  } else if (compressionType == JPEG_TYPE) {
    co_await processJpeg(reader, fb, dstRect, fbLock);
  } else
    co_await processBasicTypes(reader, fb, dstRect, compressionControl, fbLock);
}

UINT32 TightDecoder::fromCPixel(const PixelFormat &pxFormat, const UINT8 *rgb)
{
  return static_cast<UINT32>(rgb[0]) << pxFormat.redShift |
         static_cast<UINT32>(rgb[1]) << pxFormat.greenShift |
         static_cast<UINT32>(rgb[2]) << pxFormat.blueShift;
}

std::vector<UINT8> TightDecoder::transformArray(const PixelFormat &pxFormat,
                                                const std::vector<UINT8> &buffer)
{
  std::vector<UINT8> result(buffer.size() / 3 * 4);
  for (size_t bi = 0, ri = 0; bi + 3 <= buffer.size(); bi += 3, ri += 4) {
    UINT32 pixel = fromCPixel(pxFormat, &buffer[bi]);
    memcpy(&result[ri], &pixel, sizeof(pixel));
  }
  return result;
}

Task<UINT32> TightDecoder::readTightPixel(AwaitableReader &reader, const PixelFormat &pxFormat)
{
  UINT32 color = 0;
  if (!m_isCPixel) {
    UINT8 buffer[sizeof(color)];
    co_await reader.readFully(buffer, pxFormat.bitsPerPixel / 8);
    memcpy(&color, buffer, pxFormat.bitsPerPixel / 8);
  } else {
    UINT8 buffer[3];
    co_await reader.readFully(buffer, 3);
    color = fromCPixel(pxFormat, buffer);
  }
  co_return color;
}

void TightDecoder::reset()
{
  for (int i = 0; i < DECODERS_NUM; i++) {
    replaceInflater(i);
  }
}

void TightDecoder::resetDecoders(UINT8 compressionControl)
{
  for (int i = 0; i < DECODERS_NUM; i++)
    if (compressionControl & (0x01 << i)) {
      replaceInflater(i);
    }
}

void TightDecoder::replaceInflater(int i)
{
  // The new stream is made before the old one is freed, so that if that
  // throws, m_inflater[i] is unchanged.
  m_inflater[i] = std::make_unique<Inflater>();
}

Task<int> TightDecoder::readCompactSize(AwaitableReader &reader)
{
  int b = co_await reader.readUInt8();
  int size = b & 0x7F;
  if ((b & 0x80) != 0) {
    b = co_await reader.readUInt8();
    size += (b & 0x7F) << 7;
    if ((b & 0x80) != 0) {
      size += co_await reader.readUInt8() << 14;
    }
  }
  co_return size;
}

Task<> TightDecoder::processJpeg(AwaitableReader &reader,
                                 FrameBuffer &frameBuffer,
                                 const Rect &dstRect,
                                 LocalMutex &fbLock)
{
  if (!ViewerBufferSize::fitsDecoderBuffer(dstRect.getWidth(), dstRect.getHeight(),
                                            JpegDecompressor::BYTES_PER_PIXEL)) {
    throw Exception(_T("Tight JPEG rectangle is too large"));
  }

  UINT32 jpegBufLen = co_await readCompactSize(reader);
  if (jpegBufLen == 0)
    throw Exception(_T("Error in protocol: empty byffer of jpeg (tight-decoder)"));
  std::vector<UINT8> buffer;
  buffer.resize(jpegBufLen);
  co_await reader.readFully(&buffer.front(), jpegBufLen);

  if (dstRect.area() != 0) {
    std::vector<UINT8> pixels;
    pixels.resize(dstRect.area() * JpegDecompressor::BYTES_PER_PIXEL);

    try {
      m_jpeg.decompress(buffer, jpegBufLen, pixels, dstRect);
      if (m_isCPixel) {
        pixels = transformArray(frameBuffer.getPixelFormat(), pixels);
        AutoLock al(&fbLock);
        drawTightBytes(frameBuffer, pixels, dstRect);
      } else {
        AutoLock al(&fbLock);
        drawJpegBytes(frameBuffer, pixels, dstRect);
      }
    } catch (const Exception &ex) {
      StringStorage error;
      error.format(_T("Error in tight-decoder, subencoding \"jpeg\": %s"), 
                   ex.getMessage());
      m_logWriter->error(_T("%s"), error.getString());
    }
  }
}

Task<> TightDecoder::processBasicTypes(AwaitableReader &reader,
                                       FrameBuffer &fb,
                                       const Rect &dstRect,
                                       UINT8 compressionControl,
                                       LocalMutex &fbLock)
{
  int decoderId = (compressionControl & STREAM_ID_MASK) >> 4;
  int filterId = COPY_FILTER;
  if ((compressionControl & FILTER_ID_MASK) != 0) {
    filterId = co_await reader.readUInt8();
  }

  int bytesPerCPixel = fb.getBytesPerPixel();
  size_t lengthCurrentBpp = dstRect.area() * bytesPerCPixel;
  if (m_isCPixel) {
    lengthCurrentBpp = dstRect.area() * 3;
  }

  std::vector<UINT8> buffer;

  switch (filterId) {
  case COPY_FILTER:
    co_await readTightData(reader, buffer, lengthCurrentBpp, decoderId);
    if (m_isCPixel) {
      buffer = transformArray(fb.getPixelFormat(), buffer);
    }
    {
      AutoLock al(&fbLock);
      drawTightBytes(fb, buffer, dstRect);
    }
    break;

  // The "gradient" filter and "jpeg" compression may be used only
  // when bits-per-pixel value is either 16 or 32, not 8.
  case PALETTE_FILTER:
    {
      int paletteSize = co_await reader.readUInt8() + 1;
      std::vector<UINT32> palette = co_await readPalette(reader, paletteSize,
                                                         fb.getPixelFormat());
      size_t dataLength = dstRect.area();
      if (paletteSize == 2) {
        dataLength = (dstRect.getWidth() + 7) / 8 * dstRect.getHeight();
      }
      co_await readTightData(reader, buffer, dataLength, decoderId);
      AutoLock al(&fbLock);
      drawPalette(fb, palette, buffer, dstRect);
    }
    break;

  case GRADIENT_FILTER:
    co_await readTightData(reader, buffer, lengthCurrentBpp, decoderId);
    {
      AutoLock al(&fbLock);
      drawGradient(fb, buffer, dstRect);
    }
    break;

  default:
    break;
  }
}

Task<std::vector<UINT32>> TightDecoder::readPalette(AwaitableReader &reader,
                                      int paletteSize,
                                      const PixelFormat &pxFormat)
{
  std::vector<UINT32> palette(paletteSize);
  for (int i = 0; i < paletteSize; i++) {
    palette[i] = co_await readTightPixel(reader, pxFormat);
  }
  co_return palette;
}

Task<> TightDecoder::readTightData(AwaitableReader &reader,
                                   std::vector<UINT8> &buffer,
                                   size_t expectedLength,
                                   const int decoderId)
{
  if (expectedLength < MIN_SIZE_TO_COMPRESS) {
    buffer.resize(expectedLength);
    if (expectedLength != 0) {
      co_await reader.readFully(&buffer.front(), expectedLength);
    }
  } else {
    co_await readCompressedData(reader, buffer, expectedLength, decoderId);
  }
}

Task<> TightDecoder::readCompressedData(AwaitableReader &reader,
                                        std::vector<UINT8> &buffer,
                                        size_t expectedLength,
                                        const int decoderId)
{
  size_t rawDataLength = co_await readCompactSize(reader);

  std::vector<char> compressed(rawDataLength);

  // read compressed (raw) data behind space allocated for decompressed data
  if (rawDataLength != 0) {
    co_await reader.readFully(&compressed.front(), rawDataLength);

    Inflater *decoder = m_inflater[decoderId].get();
    decoder->setInput(&compressed.front(), rawDataLength);
    decoder->setUnpackedSize(expectedLength);
    decoder->inflate();

    size_t size = decoder->getOutputSize();
    // Every basic filter consumes exactly the declared rectangle's data.
    // Reject a short block before RGB conversion or drawing can read past it.
    if (size != expectedLength) {
      throw Exception(_T("Error in protocol: incorrect decompressed data size (tight-decoder)"));
    }
    const char *output = decoder->getOutput();
    buffer.resize(size);
    buffer.assign(output, output + size);
  } else {
    throw Exception(_T("Error in protocol: empty compressed data (tight-decoder)"));
  }
}

void TightDecoder::drawPalette(FrameBuffer &fb,
                               const std::vector<UINT32> &palette,
                               const std::vector<UINT8> &pixels,
                               const Rect &dstRect)
{
  // TODO: removed duplicate code (draw Tight bytes)
  int width = dstRect.getWidth();

  int bytesPerPixel = fb.getBytesPerPixel();

  int dstLength = dstRect.area();

  int x = dstRect.left;
  int y = dstRect.top;
  int stride = fb.getBytesPerRow();
  char *basePtr = (char*)fb.getBufferPtr(x, y);
  if (palette.size() == 2) {
    int offset = 8;
    int index = -1;
    for (int i = 0; i < dstLength; i++) {
      void *pixelPtr = basePtr + bytesPerPixel * (i % width) + stride * (i / width);
      if (offset == 0 || i % width == 0) {
        offset = 8;
        index++;
      }
      offset--;
      memcpy(pixelPtr, &palette[(pixels[index] >> offset) & 0x01], bytesPerPixel);
    }
  } else { // size of palette != 2
    for (int i = 0; i < dstLength; i++) {
      void *pixelPtr = basePtr + bytesPerPixel * (i % width) + stride * (i / width);
      if (pixels[i] < palette.size()) {
        memcpy(pixelPtr, &palette[pixels[i]], bytesPerPixel);
      } else {
        m_logWriter->error(_T("Tight decoder: Invalid index in palette."));
      }
    }
  }
}

void TightDecoder::drawTightBytes(FrameBuffer &fb,
                                  const std::vector<UINT8> &pixels,
                                  const Rect &dstRect)
{
  // TODO: removed duplicate code (zrle)
  int width = dstRect.getWidth();

  int bytesPerPixel = fb.getBytesPerPixel();

  int dstLength = dstRect.area();

  int x = dstRect.left;
  int y = dstRect.top;
  for (int i = 0; i < dstLength; i++) {
    void *pixelPtr = fb.getBufferPtr(x + i % width, y + i / width);
    memcpy(pixelPtr, &pixels[i * bytesPerPixel], bytesPerPixel);
  }
}

void TightDecoder::drawJpegBytes(FrameBuffer &fb,
                                 const std::vector<UINT8> &pixels,
                                 const Rect &dstRect)
{
  // TODO: removed duplicate code (draw tight bytes)
  int width = dstRect.getWidth();

  int fbBytesPerPixel = fb.getBytesPerPixel();
  int bytesPerCPixel = 3;
  PixelFormat pxFormat = fb.getPixelFormat();

  int dstLength = dstRect.area();

  int x = dstRect.left;
  int y = dstRect.top;
  for (int i = 0; i < dstLength; i++) {
    UINT8 color[4] = {0, 0, 0, 0};
    memcpy(&color, &pixels[i * bytesPerCPixel], bytesPerCPixel);
    UINT32 pixel = (((UINT32)color[0] * pxFormat.redMax + 127) / 255 << pxFormat.redShift | 
                   ((UINT32)color[1] * pxFormat.greenMax + 127) / 255 << pxFormat.greenShift |
                   ((UINT32)color[2] * pxFormat.blueMax + 127) / 255 << pxFormat.blueShift);

    void *pixelPtr = fb.getBufferPtr(x + i % width, y + i / width);
    memcpy(pixelPtr, &pixel, fbBytesPerPixel);
  }
}

/*
 *-- The "gradient" filter pre-processes pixel data with a simple algorithm
 * which converts each color component to a difference between a "predicted"
 * intensity and the actual intensity. Such a technique does not affect
 * uncompressed data size, but helps to compress photo-like images better. 
 * Pseudo-code for converting intensities to differences is the following:
 *
 *   P[i,j] := V[i-1,j] + V[i,j-1] - V[i-1,j-1];
 *   if (P[i,j] < 0) then P[i,j] := 0;
 *   if (P[i,j] > MAX) then P[i,j] := MAX;
 *   D[i,j] := V[i,j] - P[i,j];
 *
 * Here V[i,j] is the intensity of a color component for a pixel at
 * coordinates (i,j). MAX is the maximum value of intensity for a color
 * component.
 */

void TightDecoder::drawGradient(FrameBuffer &fb,
                                const std::vector<UINT8> &pixels,
                                const Rect &dstRect)
{
  typedef std::vector<UINT16> RowType;
  size_t opRowLength = dstRect.getWidth() * 3 + 3;

  std::vector<RowType> opRows(2);
  opRows[0].resize(opRowLength);
  opRows[1].resize(opRowLength);

  memset(&opRows[0].front(), 0, opRowLength * sizeof(UINT16));
  memset(&opRows[1].front(), 0, opRowLength * sizeof(UINT16));
  
  PixelFormat pxFormat = fb.getPixelFormat();
  int fbBytesPerPixel = fb.getBytesPerPixel();
  int bytesPerCPixel = fbBytesPerPixel;
  if (m_isCPixel) {
    bytesPerCPixel = 3;
  }

  int opRowIndex = 0;
  UINT16 max[3] = {pxFormat.redMax, pxFormat.greenMax, pxFormat.blueMax};
  UINT16 shift[3] = {pxFormat.redShift, pxFormat.greenShift, pxFormat.blueShift};
  size_t pixelOffset = 0;

  for (int i = 0; i < dstRect.getHeight(); ++i) {
    // exchange thisRow and prevRow:
    RowType &thisRow = opRows[opRowIndex];
    RowType &prevRow = opRows[opRowIndex = (opRowIndex + 1) % 2];

    for (size_t j = 3; j < opRowLength; j += 3, pixelOffset += bytesPerCPixel) {
      UINT8 rawColor[3];
      fillRawComponents(pxFormat, rawColor, pixels, pixelOffset);
      UINT32 color = 0;
      for (int index = 0; index < 3; index++) {
        INT32 d = prevRow[j + index] +      // "upper" pixel (from prev row)
                  thisRow[j + index - 3] -  // prev pixel
                  prevRow[j + index - 3];   // "diagonal" prev pixel
        UINT16 converted = static_cast<UINT16>(d < 0 ? 0 : d > max[index] ? max[index] : d);
        thisRow[j + index] = (converted + rawColor[index]) & max[index];
        color |= static_cast<UINT32>(thisRow[j + index] & max[index]) << shift[index];
      }
      void *pixelPtr = fb.getBufferPtr(static_cast<int>(dstRect.left + j/3 - 1),
                                        dstRect.top + i);
      memcpy(pixelPtr, &color, fbBytesPerPixel);
    }
  }
}

UINT32 TightDecoder::getRawTightColor(const PixelFormat &pxFormat,
                                      const std::vector<UINT8> &pixels,
                                      const size_t offset)
{
  if (m_isCPixel) {
    return fromCPixel(pxFormat, &pixels[offset]);
  }
  UINT32 rawColor = 0;
  memcpy(&rawColor, &pixels[offset], pxFormat.bitsPerPixel / 8);
  return rawColor;
}

void TightDecoder::fillRawComponents(const PixelFormat &pxFormat,
                                     UINT8 components[],
                                     const std::vector<UINT8> &pixels,
                                     const size_t pixelOffset)
{
  UINT32 rawColor = getRawTightColor(pxFormat, pixels, pixelOffset);
  components[0] = static_cast<UINT8>(rawColor >> pxFormat.redShift & pxFormat.redMax);
  components[1] = static_cast<UINT8>(rawColor >> pxFormat.greenShift & pxFormat.greenMax);
  components[2] = static_cast<UINT8>(rawColor >> pxFormat.blueShift & pxFormat.blueMax);
}
