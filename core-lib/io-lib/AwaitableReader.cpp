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

#include "AwaitableReader.h"

#include "DataInputStream.h"

#include "util/TextEncoding.h"

#include <algorithm>
#include <cassert>
#include <cstring>
#include <utility>

AwaitableReader::AwaitableReader()
: m_start(0),
  m_pendingDst(nullptr),
  m_pendingLen(0),
  m_request(nullptr)
{
}

Task<> AwaitableReader::readUTF8(StringStorage *storage)
{
  UINT32 sizeInBytes = co_await readUInt32();
  if (sizeInBytes > DataInputStream::MAX_UTF8_SIZE) {
    throw IOException(_T("String in the stream is too long"));
  }
  if (sizeInBytes > 0) {
    std::vector<std::byte> buffer(sizeInBytes);
    co_await readFully(buffer.data(), sizeInBytes);
    *storage = TextEncoding::fromUtf8(buffer);
  } else {
    storage->setString(_T(""));
  }
}

void AwaitableReader::append(const void *data, size_t len)
{
  // Drop the bytes read already, once they are the larger part, so that
  // the buffer does not grow with everything ever received.
  if (m_start > 0 && m_start >= m_buffer.size() - m_start) {
    m_buffer.erase(m_buffer.begin(), m_buffer.begin() + m_start);
    m_start = 0;
  }
  const std::byte *bytes = static_cast<const std::byte *>(data);
  m_buffer.insert(m_buffer.end(), bytes, bytes + len);
}

AwaitableReader::Request &AwaitableReader::takeRequest()
{
  assert(m_request != nullptr);
  return *std::exchange(m_request, nullptr);
}

void AwaitableReader::fail(std::exception_ptr error) noexcept
{
  assert(error);
  if (!m_error) {
    m_error = std::move(error);
  }
}

void AwaitableReader::resumeWaiter()
{
  assert(m_waiter);
  if (!m_error) {
    if (m_pendingLen > 0) {
      size_t copied = take(m_pendingDst, m_pendingLen);
      m_pendingDst += copied;
      m_pendingLen -= copied;
      if (m_pendingLen > 0) {
        return;
      }
    }
    assert(m_request == nullptr);
  }
  m_pendingDst = nullptr;
  m_pendingLen = 0;
  m_request = nullptr;
  std::exchange(m_waiter, {}).resume();
}

bool AwaitableReader::tryRead(std::byte *dst, size_t len)
{
  if (m_error) {
    return true;
  }
  if (m_buffer.size() - m_start < len) {
    return false;
  }
  take(dst, len);
  return true;
}

void AwaitableReader::waitForBytes(std::coroutine_handle<> waiter,
                                   std::byte *dst, size_t len)
{
  assert(!m_waiter);
  size_t copied = take(dst, len);
  m_waiter = waiter;
  m_pendingDst = dst + copied;
  m_pendingLen = len - copied;
}

void AwaitableReader::waitForRequest(std::coroutine_handle<> waiter,
                                     Request &request)
{
  assert(!m_waiter);
  m_waiter = waiter;
  m_request = &request;
}

bool AwaitableReader::isFailed() const noexcept
{
  return static_cast<bool>(m_error);
}

void AwaitableReader::throwIfFailed() const
{
  if (m_error) {
    std::rethrow_exception(m_error);
  }
}

size_t AwaitableReader::take(std::byte *dst, size_t len)
{
  size_t count = std::min(len, m_buffer.size() - m_start);
  if (count > 0) {
    memcpy(dst, m_buffer.data() + m_start, count);
    m_start += count;
    if (m_start == m_buffer.size()) {
      m_buffer.clear();
      m_start = 0;
    }
  }
  return count;
}
