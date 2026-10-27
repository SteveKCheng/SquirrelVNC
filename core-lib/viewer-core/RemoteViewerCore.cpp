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

#include "RemoteViewerCore.h"

#include "RemoteViewerCoreImpl.h"

#include <utility>

RemoteViewerCore::RemoteViewerCore(asio::any_io_executor executor, Logger *logger)
: m_impl(std::make_unique<RemoteViewerCoreImpl>(std::move(executor), logger))
{
}

RemoteViewerCore::~RemoteViewerCore()
{
}

void RemoteViewerCore::start(RfbInputGate *input, RfbOutputGate *output,
                             std::shared_ptr<CoreEventsAdapter> adapter,
                             bool sharedFlag,
                             UINT64 maxFrameBufferPixels)
{
  m_impl->start(input, output, std::move(adapter), sharedFlag, maxFrameBufferPixels);
}

void RemoteViewerCore::start(const TCHAR *host, UINT16 port,
                             std::shared_ptr<CoreEventsAdapter> adapter,
                             bool sharedFlag,
                             UINT64 maxFrameBufferPixels)
{
  m_impl->start(host, port, std::move(adapter), sharedFlag, maxFrameBufferPixels);
}

void RemoteViewerCore::start(asio::ip::tcp::socket socket,
                             std::shared_ptr<CoreEventsAdapter> adapter,
                             bool sharedFlag,
                             UINT64 maxFrameBufferPixels)
{
  m_impl->start(std::move(socket), std::move(adapter), sharedFlag, maxFrameBufferPixels);
}

void RemoteViewerCore::stop()
{
  m_impl->stop();
}

void RemoteViewerCore::setPixelFormat(const PixelFormat &viewerPixelFormat)
{
  m_impl->setPixelFormat(viewerPixelFormat);
}

void RemoteViewerCore::stopUpdating(bool isStopped)
{
  m_impl->stopUpdating(isStopped);
}

void RemoteViewerCore::refreshFrameBuffer()
{
  m_impl->refreshFrameBuffer();
}

void RemoteViewerCore::sendKeyboardEvent(bool downFlag, UINT32 key)
{
  m_impl->sendKeyboardEvent(downFlag, key);
}

void RemoteViewerCore::sendPointerEvent(UINT8 buttonMask, const Point &position)
{
  m_impl->sendPointerEvent(buttonMask, position);
}

void RemoteViewerCore::sendCutTextEvent(const StringStorage &cutText)
{
  m_impl->sendCutTextEvent(cutText);
}

void RemoteViewerCore::setPreferredEncoding(INT32 encodingType)
{
  m_impl->setPreferredEncoding(encodingType);
}

void RemoteViewerCore::allowCopyRect(bool allow)
{
  m_impl->allowCopyRect(allow);
}

void RemoteViewerCore::setJpegQualityLevel(int newJpegQualityLevel)
{
  m_impl->setJpegQualityLevel(newJpegQualityLevel);
}

void RemoteViewerCore::setCompressionLevel(int newCompressionLevel)
{
  m_impl->setCompressionLevel(newCompressionLevel);
}

void RemoteViewerCore::enableCursorShapeUpdates(bool enabled)
{
  m_impl->enableCursorShapeUpdates(enabled);
}

void RemoteViewerCore::ignoreCursorShapeUpdates(bool ignored)
{
  m_impl->ignoreCursorShapeUpdates(ignored);
}

CapabilitiesManager &RemoteViewerCore::getCapabilitiesManager()
{
  return *m_impl;
}

void RemoteViewerCore::copyFrameBuffer(FrameBuffer &dst)
{
  m_impl->copyFrameBuffer(dst);
}
