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

#include "RemoteViewerCoreImpl.h"

#include "ft-common/FTMessage.h"
#include "rfb/AuthDefs.h"
#include "rfb/TunnelDefs.h"
#include "rfb/MsgDefs.h"
#include "rfb/EncodingDefs.h"
#include "rfb/VendorDefs.h"
#include "util/ScopeExit.h"
#include "util/TextEncoding.h"
#include "network/HostResolver.h"
#include "network/TcpKeepAlive.h"
#include "network/socket/SocketException.h"

#include "AuthHandler.h"
#include "RichCursorDecoder.h"
#include "RfbParsing.h"
#include "RfbFramebufferUpdateRequestClientMessage.h"
#include "RfbCutTextEventClientMessage.h"
#include "RfbKeyEventClientMessage.h"
#include "RfbPointerEventClientMessage.h"
#include "RfbSetEncodingsClientMessage.h"
#include "RfbSetPixelFormatClientMessage.h"
#include "ViewerBufferSize.h"

#include "RawDecoder.h"
#include "CopyRectDecoder.h"
#include "RreDecoder.h"
#include "HexTileDecoder.h"
#include "TightDecoder.h"
#include "ZrleDecoder.h"

#include "JpegQualityLevel.h"
#include "CompressionLevel.h"

#include "DesktopSizeDecoder.h"
#include "LastRectDecoder.h"
#include "PointerPosDecoder.h"
#include "RichCursorDecoder.h"

#include <asio/any_completion_handler.hpp>
#include <asio/as_tuple.hpp>
#include <asio/associated_cancellation_slot.hpp>
#include <asio/bind_cancellation_slot.hpp>
#include <asio/co_spawn.hpp>
#include <asio/connect.hpp>
#include <asio/deferred.hpp>
#include <asio/experimental/parallel_group.hpp>
#include <asio/post.hpp>
#include <asio/this_coro.hpp>
#include <asio/use_awaitable.hpp>
#include <asio/write.hpp>

#include <algorithm>
#include <cassert>
#include <stdexcept>
#include <variant>

// A limit on the length of the reason sent by the server, so that a
// malicious or broken server cannot make the viewer allocate an arbitrary
// amount of memory (the others are in RfbParsing.cpp). As
// DataInputStream::readUTF8() allowed, which used to read the reason.
static const UINT32 MAX_REASON_LENGTH = 1024 * 1024;

// The most readLoop() asks the socket for at once.
static const size_t READ_CHUNK_SIZE = 64 * 1024;

//
// The IOException for an error reading the socket: with the messages the
// blocking sockets gave (the former SocketIPv4::recv()), which the adapter
// shows; and
// for a cancellation, by stop().
//
static IOException makeReadException(const std::error_code &error)
{
  if (error == asio::error::eof) {
    return IOException(_T("Connection has been gracefully closed"));
  }
  if (error == asio::error::operation_aborted) {
    return IOException(_T("The connection was stopped"));
  }
  return IOException(_T("Failed to recv data from socket."));
}

RemoteViewerCoreImpl::RemoteViewerCoreImpl(asio::any_io_executor executor, Logger *logger)
: m_logWriter(logger),
  m_port(0),
  m_executor(std::move(executor)),
  m_isStopping(false),
  m_isFinished(false),
  m_isAtMessageBoundary(false),
  m_fbUpdateNotifier(&m_frameBuffer, &m_fbLock, &m_logWriter),
  m_decoderStore(&m_logWriter),
  m_isTightEnabled(true),
  m_isUtf8ClipboardEnabled(false)
{
  init();
}

void RemoteViewerCoreImpl::init()
{
  m_decoderStore.addDecoder(std::make_unique<RawDecoder>(&m_logWriter), 0);
  m_decoderStore.addDecoder(std::make_unique<CopyRectDecoder>(&m_logWriter), 10);
  m_decoderStore.addDecoder(std::make_unique<RreDecoder>(&m_logWriter), 1);
  m_decoderStore.addDecoder(std::make_unique<HexTileDecoder>(&m_logWriter), 4);
  m_decoderStore.addDecoder(std::make_unique<TightDecoder>(&m_logWriter), 9);
  m_decoderStore.addDecoder(std::make_unique<ZrleDecoder>(&m_logWriter), 9);

  m_decoderStore.addDecoder(std::make_unique<DesktopSizeDecoder>(&m_logWriter), -1);
  m_decoderStore.addDecoder(std::make_unique<LastRectDecoder>(&m_logWriter), -1);
  m_decoderStore.addDecoder(std::make_unique<PointerPosDecoder>(&m_logWriter), -1);
  m_decoderStore.addDecoder(std::make_unique<RichCursorDecoder>(&m_logWriter), -1);
  m_input = nullptr;
  m_output = nullptr;

  m_wasStarted = false;
  m_maxFrameBufferPixels = ViewerBufferSize::DEFAULT_MAX_FRAME_BUFFER_PIXELS;
  m_wasConnected = false;
  m_isNewPixelFormat = false;
  m_isPixelFormatSent = false;
  m_isFreeze = false;
  m_isNeedRequestUpdate = true;
  m_forceFullUpdate = false;

  addClientMsgCapability(ClientMsgDefs::CLIENT_CUT_TEXT_UTF8,
    VendorDefs::TIGHTVNC,
    Utf8CutTextDefs::CLIENT_CUT_TEXT_UTF8_SIG, 
    _T("UTF-8 clipboard"));

  addClientMsgCapability(ClientMsgDefs::ENABLE_CUT_TEXT_UTF8,
    VendorDefs::TIGHTVNC,
    Utf8CutTextDefs::ENABLE_CUT_TEXT_UTF8_SIG,
    _T("enable UTF-8 clipboard"));
}

RemoteViewerCoreImpl::~RemoteViewerCoreImpl()
{
  // The connection is over, so stop() posts nothing (see start()).
  assert(m_adapterRef.expired());
  try {
    m_fbUpdateNotifier.terminate();
    m_fbUpdateNotifier.wait();
  } catch (...) {
  }
  // A socket given to a start() that failed is closed with
  // m_acceptedSocket.
}

void RemoteViewerCoreImpl::start(std::shared_ptr<CoreEventsAdapter> adapter,
                             bool sharedFlag,
                             UINT64 maxFrameBufferPixels)
{
  m_logWriter.detail(_T("Starting remote viewer core..."));

  if (adapter == nullptr) {
    throw Exception(_T("Remote viewer core is not started: adapter is 0"));
  }

  if (m_wasStarted) {
    throw Exception(_T("Remote viewer core is already started"));
  }
  m_wasStarted = true;

  m_sharedFlag = sharedFlag;
  m_maxFrameBufferPixels = maxFrameBufferPixels;
  m_adapter = adapter.get();
  m_adapterRef = adapter;

  m_fbUpdateNotifier.setAdapter(m_adapter);

  if (m_output == nullptr) {
    m_output = std::make_unique<DataOutputStream>(&m_queuedOutput);
  }
  // The connection's reference to the adapter's owner is held by the
  // completion handler.
  asio::co_spawn(m_executor, drive(),
                 asio::bind_cancellation_slot(
                   m_stopSignal.slot(),
                   [this, owner = std::move(adapter)](std::exception_ptr failure) mutable {
                     finish(failure);
                     // Released from a handler of its own, which runs once
                     // Asio has finished with the coroutine and with
                     // m_stopSignal: releasing it may delete this object.
                     asio::post(m_executor, [owner = std::move(owner)] {});
                   }));
  m_logWriter.debug(_T("Remote viewer core is started"));
}

void RemoteViewerCoreImpl::start(const TCHAR *host,
                             UINT16 port,
                             std::shared_ptr<CoreEventsAdapter> adapter,
                             bool sharedFlag,
                             UINT64 maxFrameBufferPixels)
{
  m_host = host;
  m_port = port;
  start(std::move(adapter), sharedFlag, maxFrameBufferPixels);
}
void RemoteViewerCoreImpl::start(asio::ip::tcp::socket socket,
                             std::shared_ptr<CoreEventsAdapter> adapter,
                             bool sharedFlag,
                             UINT64 maxFrameBufferPixels)
{
  assert(socket.is_open());
  m_acceptedSocket.emplace(std::move(socket));
  start(std::move(adapter), sharedFlag, maxFrameBufferPixels);
}

void RemoteViewerCoreImpl::start(InputStream *input, OutputStream *output,
                             std::shared_ptr<CoreEventsAdapter> adapter,
                             bool sharedFlag,
                             UINT64 maxFrameBufferPixels)
{
  m_input = input;
  m_output = std::make_unique<DataOutputStream>(output);
  start(std::move(adapter), sharedFlag, maxFrameBufferPixels);
}

bool RemoteViewerCoreImpl::wasStarted() const
{
  return m_wasStarted;
}

bool RemoteViewerCoreImpl::wasConnected() const
{
  return m_wasConnected;
}

void RemoteViewerCoreImpl::stop()
{
  m_isStopping = true;
  m_fbUpdateNotifier.terminate();
  if (wasStarted()) {
    // The signal is not thread-safe: it is emitted on the network thread,
    // and only while drive() may still be cancelled.
    postToLoop([this] {
      if (!m_isFinished) {
        m_stopSignal.emit(asio::cancellation_type::terminal);
      }
    });
  }
}

void RemoteViewerCoreImpl::postToLoop(std::function<void()> work)
{
  std::shared_ptr<CoreEventsAdapter> owner = m_adapterRef.lock();
  if (!owner) {
    return;
  }
  asio::post(m_executor, [owner = std::move(owner), work = std::move(work)] {
    work();
  });
}

std::shared_ptr<void> RemoteViewerCoreImpl::claimOwner()
{
  return m_adapterRef.lock();
}

void RemoteViewerCoreImpl::setPixelFormat(const PixelFormat &pixelFormat)
{
  post(PixelFormatEvent{pixelFormat});
}

bool RemoteViewerCoreImpl::updatePixelFormat()
{
  PixelFormat pxFormat;
  m_logWriter.debug(_T("Check pixel format change..."));
  if (!m_isNewPixelFormat)
    return false;
  m_isNewPixelFormat = false;
  pxFormat = m_viewerPixelFormat;

  if (!pxFormat.isValid()) {
    throw Exception(_T("Invalid pixel format"));
  }

  {
    AutoLock al(&m_fbLock);
    PixelFormat fbPixelFormat = m_frameBuffer.getPixelFormat();
    bool isEqual = pxFormat.isEqualTo(fbPixelFormat);
    // Until the first SetPixelFormat, the frame buffer format may be a
    // stand-in for a server format that is not true colour (see
    // readPixelFormat()), so that one is sent even if the formats are equal.
    if (isEqual && m_isPixelFormatSent) {
      return false;
    }
    if (!isEqual && m_frameBuffer.getBuffer() != nullptr) {
      Dimension fbDimension = m_frameBuffer.getDimension();
      setFbProperties(fbDimension, pxFormat);
    }
    m_isPixelFormatSent = true;
  }

  RfbSetPixelFormatClientMessage pixelFormatMessage(pxFormat);
  pixelFormatMessage.send(*m_output);

  return true;
}

void RemoteViewerCoreImpl::refreshFrameBuffer()
{
  post(RefreshEvent{});
}

void RemoteViewerCoreImpl::sendFbUpdateRequest(bool incremental)
{
  bool requestUpdate = m_isNeedRequestUpdate;
  m_isNeedRequestUpdate = false;
  if (!requestUpdate)
    return;

  bool isRefresh = false;
  bool isUpdateFbProperties = false;
  if (updatePixelFormat()) {
    isUpdateFbProperties = true;
  }

  if (m_isRefreshing) {
    m_isRefreshing= false;
    isRefresh = true;
  }

  bool isIncremental = incremental && !isRefresh && !isUpdateFbProperties;
  Rect updateRect;
  {
    AutoLock al(&m_fbLock);
    updateRect = m_frameBuffer.getDimension().getRect();
  }

  if (isIncremental) {
    m_logWriter.debug(_T("Sending frame buffer incremental update request [%dx%d]..."),
                      updateRect.getWidth(), updateRect.getHeight());
  } else {
    m_logWriter.debug(_T("Sending frame buffer full update request [%dx%d]..."),
                      updateRect.getWidth(), updateRect.getHeight());
  }

  RfbFramebufferUpdateRequestClientMessage fbUpdReq(isIncremental, updateRect);
  fbUpdReq.send(*m_output);
  m_logWriter.debug(_T("Frame buffer update request is sent"));
}

void RemoteViewerCoreImpl::sendKeyboardEvent(bool downFlag, UINT32 key)
{
  post(KeyEvent{downFlag, key});
}

void RemoteViewerCoreImpl::sendPointerEvent(UINT8 buttonMask,
                                        const Point &position)
{
  post(PointerEvent{buttonMask, position});
}

void RemoteViewerCoreImpl::sendCutTextEvent(const StringStorage &cutText)
{
  post(CutTextEvent{cutText});
}

void RemoteViewerCoreImpl::setPreferredEncoding(INT32 encodingType)
{
  post(PreferredEncodingEvent{encodingType});
}

void RemoteViewerCoreImpl::allowCopyRect(bool allow)
{
  post(CopyRectEvent{allow});
}

void RemoteViewerCoreImpl::allowUtf8Clipboard()
{
  m_isUtf8ClipboardEnabled = m_clientMsgCaps.isEnabled(ClientMsgDefs::CLIENT_CUT_TEXT_UTF8);
  if (m_isUtf8ClipboardEnabled) {
    m_logWriter.debug(_T("Server supports Utf8 Clipboard recieving."));
  }
  else {
    m_logWriter.debug(_T("Server does not support Utf8 Clipboard recieving, ClientCutTextUtf8 messages disabled."));
  }
  if (m_clientMsgCaps.isEnabled(ClientMsgDefs::ENABLE_CUT_TEXT_UTF8)) {
    m_logWriter.debug(_T("Sending EnableCutTextUtf8 message."));
    m_output->writeUInt32(ClientMsgDefs::ENABLE_CUT_TEXT_UTF8);
    m_output->flush();
  }
}

void RemoteViewerCoreImpl::setCompressionLevel(int newLevel)
{
  post(CompressionLevelEvent{newLevel});
}

void RemoteViewerCoreImpl::setJpegQualityLevel(int newLevel)
{
  post(JpegQualityEvent{newLevel});
}

void RemoteViewerCoreImpl::enableCursorShapeUpdates(bool enabled)
{
  post(CursorShapeUpdatesEvent{enabled});
}

void RemoteViewerCoreImpl::ignoreCursorShapeUpdates(bool ignored)
{
  post(IgnoreCursorShapeUpdatesEvent{ignored});
}

void RemoteViewerCoreImpl::stopUpdating(bool isStopped)
{
  post(StopUpdatingEvent{isStopped});
}

Task<> RemoteViewerCoreImpl::authenticate()
{
  m_logWriter.detail(_T("Negotiating security type..."));
  int authenticationType = co_await negotiateSecurityType();
  m_logWriter.info(_T("Authentication type accepted: %s (%d)"),
                   getAuthenticationTypeName(authenticationType).getString(),
                   authenticationType);

  if (authenticationType != 0) {
    m_logWriter.detail(_T("Authentication..."));
    if (m_authHandlers.find(authenticationType) != m_authHandlers.end()) {
      co_await m_authHandlers[authenticationType]->authenticate(m_reader, m_output.get());
    } else {
      // Security type is added automatic, but not by user.
      if (authenticationType != SecurityDefs::NONE) {
        m_logWriter.error(_T("Isn't exist authentication handler for selected security type %d"),
                          authenticationType);
        throw AuthException(_T("Isn't exist authentication handler ")
                            _T("for selected type of authentication"));
      }
    }
  }

  // get authentication result, if version 3.8 or authentication isn't None
  if (m_minor >= 8 || authenticationType != SecurityDefs::NONE) {
    UINT32 authResult = 0;
    if (authenticationType) {
      authResult = co_await m_reader.readUInt32();
      m_logWriter.detail(_T("Auth result is %u"), authResult);
    }
    static const UINT32 AUTH_RESULT_OK = 0;
    if (!authenticationType || authResult != AUTH_RESULT_OK) {
      // if version 3.3 or 3.7 then server connection closed
      m_logWriter.message(_T("Authentication failure"));
      if (m_minor < 8) {
        throw AuthException(_T("Authentication failure"));
      }
      // if version 3.8 then try read reasonAuth.
      StringStorage reasonAuth = co_await readReason();
      StringStorage errorMessage = _T("Authentication reason: ");
      errorMessage.appendString(reasonAuth.getString());
      m_logWriter.message(_T("%s"), errorMessage.getString());
      throw AuthException(errorMessage.getString());
    }
  }
}

Task<int> RemoteViewerCoreImpl::negotiateSecurityType()
{
  m_logWriter.detail(_T("Reading list of security types..."));
  // read list of security types
  std::vector<UINT32> secTypes;
  co_await readSecurityTypeList(&secTypes);
  m_logWriter.debug(_T("List of security type is read"));
  if (secTypes.size() == 0) {
    m_logWriter.warning(_T("Error in negotiate about of security: only security type is 0"));
    co_return 0;
  }

  // log information about security list
  StringStorage secTypeString;
  for (std::vector<UINT32>::iterator i = secTypes.begin(); i != secTypes.end(); i++) {
    if(i != secTypes.begin())
      secTypeString.appendString(_T(", "));
    StringStorage nameType;
    nameType.format(_T("%s (%u)"), getSecurityTypeName(*i).getString(), *i);
    secTypeString.appendString(nameType.getString());
  }
  m_logWriter.detail(_T("Security Types received (%zu): %s"),
                   secTypes.size(), secTypeString.getString()); 

  // select type security
  m_logWriter.debug(_T("Selecting auth-handler"));
  int typeSelected = selectSecurityType(secTypes, m_authHandlers, m_isTightEnabled);
  m_logWriter.info(_T("Security type is selected: %d"), typeSelected);
  if (typeSelected == SecurityDefs::TIGHT) {
    m_logWriter.info(_T("Tight capabilities is enable"));
    m_isTight = true;

    m_output->writeUInt8(static_cast<UINT8>(typeSelected));
    m_output->flush();

    co_await initTunnelling();
    co_return co_await initAuthentication();
  }

  if (m_minor >= 7) {
    // In RFB 3.7 and later, security types are 8 bits.
    m_output->writeUInt8(static_cast<UINT8>(typeSelected));
    m_output->flush();
  }

  co_return typeSelected;
}

Task<> RemoteViewerCoreImpl::readSecurityTypeList(std::vector<UINT32> *secTypes)
{
  if (m_minor < 7) {
    UINT32 type = co_await m_reader.readUInt32();
    if (type != 0)
      secTypes->push_back(type);
  } else { // m_minor >= 7
    int secTypesNumber = co_await m_reader.readUInt8();
    if (secTypesNumber != 0) {
      secTypes->resize(secTypesNumber);
      for (std::vector<UINT32>::iterator i = secTypes->begin(); i != secTypes->end(); i++)
        *i = co_await m_reader.readUInt8();
    }
  }
}

StringStorage RemoteViewerCoreImpl::getSecurityTypeName(UINT32 securityType) const
{
  switch (securityType) {
  case SecurityDefs::NONE:
    return _T("None");
  case SecurityDefs::VNC:
    return _T("VNC");
  case SecurityDefs::TIGHT:
    return _T("Tight");
  }
  return _T("Unknown type");
}

StringStorage RemoteViewerCoreImpl::getAuthenticationTypeName(UINT32 authenticationType) const
{
  switch (authenticationType) {
  case AuthDefs::NONE:
    return _T("None");
  case AuthDefs::VNC:
    return _T("VNC");
  }
  return _T("Unknown type");
}

int RemoteViewerCoreImpl::selectSecurityType(const std::vector<UINT32> &secTypes,
                                         const std::map<UINT32, AuthHandler *> &authHandlers,
										 const bool isTightEnabled) const
{
  // Typedef const iterator.
  typedef std::vector<UINT32>::const_iterator SecTypesIterator;

  if (isTightEnabled) {
    // If server supports security type "Tight", select it.
    SecTypesIterator tightSecType = std::find(secTypes.begin(),
                                              secTypes.end(),
                                              SecurityDefs::TIGHT);
    if (tightSecType != secTypes.end()) {
      return *tightSecType;
    }
  }
  // If server doesn't support security type "Tight", then search first type, which is supported.
  for (SecTypesIterator i = secTypes.begin();
       i != secTypes.end();
       i++) {
    if (authHandlers.find(*i) != authHandlers.end() ||
        *i == SecurityDefs::NONE)
      return *i;
  }

  throw Exception(_T("No security types supported. ")
                  _T("Server sent security types, ")
                  _T("but we do not support any of them."));
}

Task<> RemoteViewerCoreImpl::initTunnelling()
{
  m_logWriter.detail(_T("Initialization of tight-tunneling..."));
  UINT32 tunnelCount = co_await m_reader.readUInt32();
  if (tunnelCount > 0) {
    bool hasNoTunnel = false;
    for (UINT32 i = 0; i < tunnelCount; i++) {
      RfbCapabilityInfo cap = co_await readCapability();
      if (cap.code == TunnelDefs::NOTUNNEL) {
        hasNoTunnel = true;
      }
      // Special case for VNC server of Siemense PLC . It supports NOTUNNEL while there is no it in the list.
	  if (cap.isEqual("SICR", "SCHANNEL")) {
        hasNoTunnel = true;
      }
    }
    if (hasNoTunnel) {
      m_output->writeUInt32(TunnelDefs::NOTUNNEL);
      m_output->flush();
    } else {
      m_logWriter.error(_T("Viewer support only default tunneling tight-authentication"));
      throw Exception(_T("Viewer support only default tunneling tight-authentication"));
    }
  }
  m_logWriter.debug(_T("Tunneling is init"));
}

Task<int> RemoteViewerCoreImpl::initAuthentication()
{
  m_logWriter.detail(_T("Initialization of tight-authentication..."));
  UINT32 authTypesNumber = co_await m_reader.readUInt32();

  m_logWriter.info(_T("Number of auth-types is %u"), authTypesNumber);
  if (authTypesNumber == 0) {
    co_return AuthDefs::NONE;
  }

  CapsContainer newerViewerAuthCaps;
  newerViewerAuthCaps.add(AuthDefs::SRP6DUAL, VendorDefs::TIGHTVNC,
                          AuthDefs::SIG_SRP6DUAL);

  m_logWriter.detail(_T("Reading authentication capability..."));
  for (UINT32 i = 0; i < authTypesNumber; i++) {
    RfbCapabilityInfo cap = co_await readCapability();
    m_authCaps.enable(cap);
    newerViewerAuthCaps.enable(cap);
  }
  size_t numEnabled = m_authCaps.numEnabled();
  std::vector<UINT32> authTypes(numEnabled);
  for (size_t i = 0; i < numEnabled; ++i) {
    authTypes[i] = m_authCaps.getByOrder(i);
  }

  m_logWriter.debug(_T("Authentication capability is read"));

  m_logWriter.debug(_T("Select authentication capability"));
  if (numEnabled == 0) {
    if (newerViewerAuthCaps.isEnabled(AuthDefs::SRP6DUAL)) {
      throw Exception(_T("The server offers only SRP-6 authentication, ")
                      _T("which this viewer does not support. ")
                      _T("Enable VNC password authentication on the server ")
                      _T("(\"Legacy VNC authentication\" in TightVNC Server 4)."));
    }
    throw Exception(_T("The server does not offer a compatible authentication method. ")
                    _T("Check the server's authentication settings."));
  }
  int typeSelected = authTypes[0];
  m_logWriter.detail(_T("Selected type of authentication: %d"), typeSelected);

  m_output->writeUInt32(typeSelected);
  m_output->flush();

  m_logWriter.debug(_T("Tight authentication is init"));
  co_return typeSelected;
}

void RemoteViewerCoreImpl::setFbProperties(const Dimension &fbDimension,
                                       const PixelFormat &fbPixelFormat)
{
  RfbParsing::checkFrameBufferProperties(fbDimension, fbPixelFormat,
                                       m_maxFrameBufferPixels);

  const PixelFormat &pxFormat = fbPixelFormat;
  StringStorage pxString;
  pxString.format(_T("[bits-per-pixel: %d, depth: %d, big-endian-flag: %d, ")
                  _T("true-color-flag: is set, ") // true color always is set
                  _T("red-max: %d, green-max: %d, blue-max: %d, ")
                  _T("red-shift: %d, green-shift: %d, blue-shift: %d]"),
                  pxFormat.bitsPerPixel, pxFormat.colorDepth, pxFormat.bigEndian,
                  pxFormat.redMax, pxFormat.greenMax, pxFormat.blueMax,
                  pxFormat.redShift, pxFormat.greenShift, pxFormat.blueShift);

  m_logWriter.detail(_T("Setting frame buffer properties..."));
  m_logWriter.info(_T("Frame buffer dimension: (%d, %d)"),
                   fbDimension.width, fbDimension.height);
  m_logWriter.info(_T("Frame buffer pixel format: %s"), pxString.getString());

  if (!m_frameBuffer.setProperties(fbDimension, fbPixelFormat) ||
      !m_rectangleFb.setProperties(fbDimension, fbPixelFormat)) {
    StringStorage error;
    error.format(_T("Failed to set property frame buffer. ")
                 _T("Dimension: (%d, %d), Pixel format: %s"),
                 fbDimension.width, fbDimension.height,
                 pxString.getString());
    throw Exception(error.getString());
  }
  m_rectangleFb.setColor(0, 0, 0);
  m_frameBuffer.setColor(0, 0, 0);
  refreshFrameBuffer();
  m_fbUpdateNotifier.onPropertiesFb();
  m_logWriter.debug(_T("Frame buffer properties set"));
}

StringStorage RemoteViewerCoreImpl::getProtocolString() const
{
  StringStorage protocolString;
  protocolString.format(_T("RFB %03d.%03d\n"), m_major, m_minor);
  return protocolString;
}


Task<> RemoteViewerCoreImpl::run()
{
  // get server version and set client version
  m_logWriter.info(_T("Protocol stage is \"Handshake\"."));
  co_await handshake();

  // negotiaty about security type and authenticate
  m_logWriter.info(_T("Protocol stage is \"Authentication\"."));
  co_await authenticate();

  // set shared flag, get server dimension, pixel format and hostname
  // send client pixel format and set him.
  m_logWriter.info(_T("Protocol stage is \"Initialization\"."));
  co_await clientAndServerInit();

  // is connected
  m_logWriter.info(_T("Protocol stage is \"Is connected\"."));

  try {
    ConnectionInfo info;
    info.desktopName = m_remoteDesktopName;
    info.protocolString = getProtocolString();
    getEnabledClientMsgCapabilities(&info.clientMsgCodes);
    getEnabledServerMsgCapabilities(&info.serverMsgCodes);
    m_adapter->onConnected(m_output.get(), info);
  } catch (const Exception &ex) {
    m_logWriter.error(_T("Error in CoreEventsAdapter::onConnected(): %s"), ex.getMessage());
  } catch (...) {
    m_logWriter.error(_T("Unknown error in CoreEventsAdapter::onConnected()"));
  }

  // The settings queued so far, before m_wasConnected is set, so that
  // their handlers leave sending SetEncodings to sendEncodings() below.
  applyPendingEvents();

  m_wasConnected = true;

  // send supporting encoding
  m_logWriter.info(_T("Protocol stage is \"Encoding select\"."));
  sendEncodings();

  // send ENABLE_CUT_TEXT_UTF8 if server has the capability
  allowUtf8Clipboard();

  // send request of frame buffer update
  m_logWriter.info(_T("Protocol stage is \"Working phase\"."));
  sendFbUpdateRequest(false);

  // received server messages
  while (!m_isStopping) {
    applyPendingEvents();
    m_isAtMessageBoundary = true;
    UINT32 msgType = co_await RfbParsing::readServerMessageType(m_reader);
    m_isAtMessageBoundary = false;

    switch (msgType) {
    case ServerMsgDefs::FB_UPDATE:
      m_logWriter.detail(_T("Received message: FB_UPDATE"));
      co_await receiveFbUpdate();
      break;

    case ServerMsgDefs::SET_COLOR_MAP_ENTRIES:
      m_logWriter.detail(_T("Received message: SET_COLOR_MAP_ENTRIES"));
      co_await RfbParsing::readSetColorMapEntries(m_reader);
      break;

    case ServerMsgDefs::BELL:
      m_logWriter.detail(_T("Received message: BELL"));
      receiveBell();
      break;

    case ServerMsgDefs::SERVER_CUT_TEXT:
      m_logWriter.detail(_T("Received message: SERVER_CUT_TEXT"));
      co_await receiveServerCutText();
      break;
    case ServerMsgDefs::SERVER_CUT_TEXT_UTF8:
      m_logWriter.detail(_T("Received message: SERVER_CUT_TEXT_UTF8"));
      co_await receiveServerCutTextUtf8();
      break;

    default:
      if (m_serverMsgHandlers.find(msgType) != m_serverMsgHandlers.end()) {
        m_logWriter.detail(_T("Received message (%u) transmit to capability handler"), msgType);
        try {
          co_await m_serverMsgHandlers[msgType]->onServerMessage(msgType, m_reader);
        } catch (const Exception &ex) {
          m_logWriter.error(_T("Error in onServerMessage(): %s"), ex.getMessage());
        } catch (...) {
          m_logWriter.error(_T("Unknown error in onServerMessage()"));
        }
      } else {
        m_logWriter.error(_T("Server to client message: %u is not supported"), msgType);
      }
    }
  }
  StringStorage message(_T("Remote viewer's core thread terminated"));
  try {
    m_adapter->onDisconnect(message);
  } catch (const Exception &ex) {
    m_logWriter.error(_T("Error in CoreEventsAdapter::onDisconnect(): %s"), ex.getMessage());
  } catch (...) {
    m_logWriter.error(_T("Unknown error in CoreEventsAdapter::onDisconnect()"));
  }

}

asio::awaitable<void> RemoteViewerCoreImpl::drive()
{
  auto notifyEstablished = [this] {
    m_logWriter.detail(_T("Connection is established"));
    try {
      m_adapter->onEstablished();
    } catch (const Exception &ex) {
      m_logWriter.error(_T("Error in CoreEventsAdapter::onEstablished(): %s"), ex.getMessage());
    } catch (...) {
      m_logWriter.error(_T("Unknown error in CoreEventsAdapter::onEstablished()"));
    }
  };

  if (m_input != nullptr) {
    // Streams, for tests: the input is in memory, so the blocking reads of
    // runBlocking() do not hold up the loop for long.
    notifyEstablished();
    runBlocking(run(), m_reader, *m_input);
    co_return;
  }

  // Whatever happens, nothing is queued or woken once the connection ends.
  ScopeExit closeQueue([this] { m_queuedOutput.close(); });

  m_logWriter.info(_T("Protocol stage is \"Connection establishing\"."));
  co_await connect();
  notifyEstablished();

  m_writeWake.emplace(m_executor);
  m_queuedOutput.open([this] {
    postToLoop([this] {
      m_writeWake->cancel();
    });
  });

  // Until the first of them ends; the other is then cancelled, and waited
  // for (notes/async-networking.md, "Corrections and decisions", 3).
  auto [order, readError, writeError] =
    co_await asio::experimental::make_parallel_group(
      asio::co_spawn(m_executor, readLoop(), asio::deferred),
      asio::co_spawn(m_executor, writeLoop(), asio::deferred))
    .async_wait(asio::experimental::wait_for_one(), asio::use_awaitable);

  // If the protocol ended by itself (the server closed the connection, or
  // the protocol failed), rather than by stop() or a failed send, send what
  // it queued last: writeLoop() may not have been woken for it before it
  // was cancelled, and the server may have closed only its sending side.
  if (order[0] == 0 && !writeError && !m_isStopping) {
    std::vector<char> rest = m_queuedOutput.take();
    if (!rest.empty()) {
      co_await asio::async_write(*m_socket, asio::buffer(rest),
                                 asio::as_tuple(asio::use_awaitable));
    }
  }

  std::error_code ignored;
  m_socket->shutdown(asio::ip::tcp::socket::shutdown_both, ignored);
  m_socket->close(ignored);

  if (readError) {
    std::rethrow_exception(readError);
  }
  if (writeError) {
    std::rethrow_exception(writeError);
  }
  if (m_runError) {
    std::rethrow_exception(m_runError);
  }
}

asio::awaitable<void> RemoteViewerCoreImpl::connect()
{
  auto throwConnectError = [](const std::error_code &error) {
    if (error == asio::error::operation_aborted) {
      throw IOException(_T("The connection was stopped"));
    }
    throw SocketException(error.value());
  };

  if (m_acceptedSocket) {
    m_socket.emplace(std::move(*m_acceptedSocket));
    m_acceptedSocket.reset();
  } else {
    m_socket.emplace(m_executor);
    auto [resolveError, endpoints] =
      co_await asyncResolve(m_executor, m_host, m_port, asio::as_tuple(asio::use_awaitable));
    if (resolveError) {
      throwConnectError(resolveError);
    }
    m_logWriter.detail(_T("Connecting to the host \"%s:%hu\" (%hs)..."),
                       m_host.getString(), m_port,
                       endpoints.front().address().to_string().c_str());
    auto [connectError, endpoint] =
      co_await asio::async_connect(*m_socket, endpoints, asio::as_tuple(asio::use_awaitable));
    if (connectError) {
      throwConnectError(connectError);
    }
  }
  configureSocket();
}

void RemoteViewerCoreImpl::configureSocket()
{
  std::error_code error;
  m_socket->set_option(asio::ip::tcp::no_delay(true), error);
  if (error) {
    throw SocketException(error.value());
  }
  try {
    enableTcpKeepAlive(m_socket->native_handle(),
                       KEEPALIVE_IDLE_SECONDS,
                       KEEPALIVE_INTERVAL_SECONDS,
                       KEEPALIVE_PROBE_COUNT);
  } catch (const SocketException &e) {
    m_logWriter.error(_T("Cannot turn on TCP keepalive: %s"), e.getMessage());
  }
}

asio::awaitable<void> RemoteViewerCoreImpl::readLoop()
{
  Task<> root = run();
  root.start();
  std::vector<char> chunk(READ_CHUNK_SIZE);
  while (!root.isDone()) {
    if (!m_reader.isWaiting()) {
      // A bug: nothing here can resume the protocol code.
      throw std::logic_error("RemoteViewerCoreImpl::readLoop(): the protocol waits for "
                             "something other than the reader");
    }
    try {
      if (m_reader.hasRequest()) {
        std::exception_ptr failure = co_await serveRequest(m_reader.takeRequest());
        if (failure) {
          std::rethrow_exception(failure);
        }
      } else {
        auto [error, length] =
          co_await m_socket->async_read_some(asio::buffer(chunk),
                                             asio::as_tuple(asio::use_awaitable));
        if (error) {
          if (error != asio::error::eof && error != asio::error::operation_aborted) {
            m_logWriter.error(_T("Reading the socket failed (error %d)"), error.value());
          }
          throw makeReadException(error);
        }
        m_reader.append(chunk.data(), length);
      }
    } catch (const std::system_error &e) {
      // A co_await after a cancellation throws it (asio::this_coro::
      // throw_if_cancelled); the protocol code expects an IOException.
      m_reader.fail(std::make_exception_ptr(makeReadException(e.code())));
    } catch (...) {
      m_reader.fail(std::current_exception());
    }
    m_reader.resumeWaiter();
  }
  try {
    root.result();
  } catch (...) {
    m_runError = std::current_exception();
  }
}

asio::awaitable<std::exception_ptr> RemoteViewerCoreImpl::serveRequest(AwaitableReader::Request &request)
{
  // The handler of the operation, shared by the request's done function
  // (which a std::function must be able to copy) and taken once.
  using Handler = asio::any_completion_handler<void(std::exception_ptr)>;
  // as_tuple: with use_awaitable alone, the exception would be thrown here.
  auto token = asio::as_tuple(asio::use_awaitable);
  auto [failure] = co_await asio::async_initiate<decltype(token), void(std::exception_ptr)>(
    [this, &request](Handler handler) {
      auto shared = std::make_shared<Handler>(std::move(handler));
      asio::cancellation_slot slot = asio::get_associated_cancellation_slot(*shared);
      if (slot.is_connected()) {
        // stop(): the request ends early, and done is still called.
        slot.assign([&request](asio::cancellation_type) {
          request.cancel();
        });
      }
      request.start([this, shared, slot](std::exception_ptr failure) mutable {
        // Any thread: back to the network thread, with a reference that
        // keeps the core alive until the handler has run.
        postToLoop([shared, slot, failure]() mutable {
          if (slot.is_connected()) {
            slot.clear();
          }
          if (*shared) {
            Handler handler = std::move(*shared);
            std::move(handler)(failure);
          }
        });
      });
    },
    token);
  co_return failure;
}

asio::awaitable<void> RemoteViewerCoreImpl::writeLoop()
{
  for (;;) {
    // Only input is queued there.
    for (const ClientEvent &event : m_inputEvents.take()) {
      if (const KeyEvent *key = std::get_if<KeyEvent>(&event)) {
        send(*key);
      } else if (const PointerEvent *pointer = std::get_if<PointerEvent>(&event)) {
        send(*pointer);
      } else if (const CutTextEvent *cutText = std::get_if<CutTextEvent>(&event)) {
        send(*cutText);
      }
    }
    std::vector<char> bytes = m_queuedOutput.take();
    if (bytes.empty()) {
      // Woken by being cancelled: by a write, or by the end.
      m_writeWake->expires_at(asio::steady_timer::time_point::max());
      co_await m_writeWake->async_wait(asio::as_tuple(asio::use_awaitable));
    } else {
      auto [error, length] =
        co_await asio::async_write(*m_socket, asio::buffer(bytes),
                                   asio::as_tuple(asio::use_awaitable));
      if (error && error != asio::error::operation_aborted) {
        m_logWriter.error(_T("Writing the socket failed (error %d)"), error.value());
        m_reader.fail(std::make_exception_ptr(
          IOException(_T("Failed to send data to socket."))));
        co_return;
      }
    }
    asio::cancellation_state state = co_await asio::this_coro::cancellation_state;
    if (state.cancelled() != asio::cancellation_type::none) {
      co_return;
    }
  }
}

void RemoteViewerCoreImpl::finish(std::exception_ptr failure)
{
  if (failure) {
    try {
      std::rethrow_exception(failure);
    } catch (const AuthException &ex) {
      m_logWriter.message(_T("RemoteVewerCore. Auth exception: %s"), ex.getMessage());
      try {
        m_adapter->onAuthError(ex);
      } catch (const Exception &ex) {
        m_logWriter.error(_T("Error in CoreEventsAdapter::onAuthError(): %s"), ex.getMessage());
      } catch (...) {
        m_logWriter.error(_T("Unknown error in CoreEventsAdapter::onAuthError()"));
      }
    } catch (const IOException &ex) {
      try {
        StringStorage disconnectMessage(ex.getMessage());
        m_adapter->onDisconnect(disconnectMessage);
      } catch (const Exception &ex) {
        m_logWriter.error(_T("Error in CoreEventsAdapter::onDisconnect(): %s"), ex.getMessage());
      } catch (...) {
        m_logWriter.error(_T("Unknown error in CoreEventsAdapter::onDisconnect()"));
      }
    } catch (const Exception &ex) {
      m_logWriter.message(_T("RemoteViewerCore. Exception: %s"), ex.getMessage());
      try {
        m_adapter->onError(ex);
      } catch (...) {
        m_logWriter.error(_T("Unknown error in CoreEventsAdapter::onError()"));
      }
    } catch (...) {
      StringStorage error;
      error.format(_T("RemoteViewerCore. Unknown exception"));
      m_logWriter.message(_T("%s"), error.getString());
      Exception ex(error.getString());
      try {
        m_adapter->onError(ex);
      } catch (...) {
        m_logWriter.error(_T("Unknown error in CoreEventsAdapter::onError()"));
      }
    }
  }
  m_isFinished = true;
}

Task<> RemoteViewerCoreImpl::receiveFbUpdate()
{
  // message type is already known: 0

  UINT16 numberOfRectangles = co_await RfbParsing::readFbUpdateHeader(m_reader);
  m_logWriter.debug(_T("number of rectangles: %d"), numberOfRectangles);

  bool isLastRect = false;
  for (int rectangle = 0; rectangle < numberOfRectangles && !isLastRect; rectangle++) {
    m_logWriter.debug(_T("Receiving rectangle #%d..."), rectangle);
    isLastRect = co_await receiveFbUpdateRectangle();
  }

  m_isNeedRequestUpdate = true;
  if (m_isFreeze)
    co_return;
  m_logWriter.detail(_T("Sending of frame buffer update request..."));
  sendFbUpdateRequest(!m_forceFullUpdate);
}

Task<bool> RemoteViewerCoreImpl::receiveFbUpdateRectangle()
{
  RfbParsing::RectangleHeader header = co_await RfbParsing::readRectangleHeader(m_reader);
  const Rect &rect = header.rect;
  int encodingType = header.encodingType;

  m_logWriter.debug(_T("Rectangle: (%d, %d), (%d, %d). Type is %d"), 
                    rect.left, rect.top, rect.right, rect.bottom, encodingType);

  if (encodingType == PseudoEncDefs::LAST_RECT)
    co_return true;
  if (!Decoder::isPseudo(encodingType)) {
    RfbParsing::checkRectangleInside(m_frameBuffer.getDimension(), rect);

    Decoder *decoder = m_decoderStore.getDecoder(encodingType);
    if (decoder != nullptr) {
      m_logWriter.debug(_T("Decoding..."));

      DecoderOfRectangle *rectangleDecoder = dynamic_cast<DecoderOfRectangle *>(decoder);
      co_await rectangleDecoder->process(m_reader,
                                         m_frameBuffer, m_rectangleFb, rect, &m_fbLock,
                                         &m_fbUpdateNotifier);

      m_logWriter.debug(_T("Decoded"));
    } else { // decoder is 0
      StringStorage errorString;
      errorString.format(_T("Decoder \"%d\" isn't exist"), encodingType);
      m_logWriter.error(_T("%s"), errorString.getString());
      throw Exception(errorString.getString());
    } 
  } else { // it's pseudo encoding
    m_logWriter.debug(_T("It's pseudo encoding"));
    co_await processPseudoEncoding(rect, encodingType);
  }
  co_return false;
}

Task<> RemoteViewerCoreImpl::processPseudoEncoding(const Rect &rect,
                                               int encodingType)
{
  switch (encodingType) {
  case PseudoEncDefs::DESKTOP_SIZE:
    m_logWriter.info(_T("Changed size of desktop"));
    {
      AutoLock al(&m_fbLock);
      Dimension dimension(rect);
      PixelFormat pixelFormat = m_frameBuffer.getPixelFormat();
      setFbProperties(dimension, pixelFormat);
    }
    break;
    
  case PseudoEncDefs::RICH_CURSOR:
    {
      m_logWriter.detail(_T("New rich cursor"));

      RfbParsing::RichCursor cursor =
        co_await RfbParsing::readRichCursor(m_reader, rect,
                                          m_frameBuffer.getBytesPerPixel());

      m_logWriter.debug(_T("Setting new rich cursor..."));
      m_fbUpdateNotifier.setNewCursor(cursor.hotSpot, cursor.width, cursor.height,
                                      cursor.pixels, cursor.bitmask);
    }
    break;

  case PseudoEncDefs::POINTER_POS:
    {
      m_logWriter.detail(_T("Updating pointer position: [%d, %d]"), rect.left, rect.top);
      Point position(rect.left, rect.top);
      m_fbUpdateNotifier.updatePointerPos(position);
    }
    break;

  default:
    StringStorage errorString;
    errorString.format(_T("Pseudo encoding %d is not supported"), encodingType);
    m_logWriter.error(_T("%s"), errorString.getString());
    throw Exception(errorString.getString());
  }
}

void RemoteViewerCoreImpl::receiveBell()
{
  // message is already readed. Message type: 2

  m_logWriter.info(_T("Bell!"));
  try {
    m_adapter->onBell();
  } catch (const Exception &ex) {
    m_logWriter.error(_T("Error in CoreEventsAdapter::onBell(): %s"), ex.getMessage());
  } catch (...) {
    m_logWriter.error(_T("Unknown error in CoreEventsAdapter::onBell()"));
  }
}

Task<StringStorage> RemoteViewerCoreImpl::readReason()
{
  UINT32 length = co_await m_reader.readUInt32();
  if (length > MAX_REASON_LENGTH) {
    throw IOException(_T("String in the stream is too long"));
  }
  std::vector<std::byte> buffer(length);
  co_await m_reader.readFully(buffer.data(), length);
  co_return TextEncoding::fromRfbText(buffer);
}

Task<> RemoteViewerCoreImpl::receiveServerCutText()
{
  // message type is already known: 3

  std::optional<StringStorage> cutText =
    co_await RfbParsing::readServerCutText(m_reader, m_logWriter);
  if (!cutText)
    co_return;

  // Only the length: the clipboard may hold passwords.
  m_logWriter.debug(_T("Cut text: %zu characters"), cutText->getLength());
  try {
    m_adapter->onCutText(*cutText);
  } catch (const Exception &ex) {
    m_logWriter.error(_T("Error in CoreEventsAdapter::onCutText(): %s"), ex.getMessage());
  } catch (...) {
    m_logWriter.error(_T("Unknown error in CoreEventsAdapter::onCutText()"));
  }
}

Task<> RemoteViewerCoreImpl::receiveServerCutTextUtf8()
{
  std::optional<StringStorage> cutText =
    co_await RfbParsing::readServerCutTextUtf8(m_reader, m_logWriter);
  if (!cutText)
    co_return;

  // Only the length: the clipboard may hold passwords.
  m_logWriter.debug(_T("Cut text: %zu characters"), cutText->getLength());
  try {
    m_adapter->onCutText(*cutText);
  }
  catch (const Exception &ex) {
    m_logWriter.error(_T("Error in CoreEventsAdapter::onCutText(): %s"), ex.getMessage());
  }
  catch (...) {
    m_logWriter.error(_T("Unknown error in CoreEventsAdapter::onCutText()"));
  }
}

bool RemoteViewerCoreImpl::isRfbProtocolString(const char protocol[12]) const
{
  // Format protocol version "RFB XXX.YYY\n"
  // where XXX is major version and YYY is minor version of protocol
  // TODO: replace "if" to regexp

  if (strncmp(protocol, "RFB ", 4) != 0)
    return false;

  if (!isdigit(protocol[4]) || !isdigit(protocol[5]) || !isdigit(protocol[6]))
    return false;

  if (protocol[7] != '.')
    return false;

  if (!isdigit(protocol[8]) || !isdigit(protocol[9]) || !isdigit(protocol[10]))
    return false;

  if (protocol[11] != '\n')
    return false;

  return true;
}

Task<> RemoteViewerCoreImpl::handshake()
{
  char serverProtocol[13];
  serverProtocol[12] = 0;
  co_await m_reader.readFully(serverProtocol, 12);

  m_major = strtol(&serverProtocol[4], nullptr, 10);
  m_minor = strtol(&serverProtocol[8], nullptr, 10);
  m_isTight = false;

  m_logWriter.info(_T("Server sent protocol version: %s"), getProtocolString().getString());
  if (!isRfbProtocolString(serverProtocol) || 
      m_major < 3 ||
      (m_major == 3 && m_minor < 3)) {
    StringStorage error;
    StringStorage protocol =
      TextEncoding::fromLatin1(std::as_bytes(std::span(serverProtocol, 12)));
    error.format(_T("Unsupported protocol: %s"), protocol.getString());
    m_logWriter.error(_T("%s"), error.getString());
    throw Exception(error.getString());
  }

  // if version is 4.0 or later, then set version 3.8.
  if (m_major > 3) {
    m_major = 3;
    m_minor = 8;
  } else { // else set version from list 3.3, 3.7, 3.8.
    // select minor version:
    if (m_minor < 7) {
      m_minor = 3;
    }
    if (m_minor >= 8) {
     m_minor = 8;
    }
  }

  m_logWriter.info(_T("Send to server protocol version: %s"), getProtocolString().getString());

  // ASCII, and 12 bytes, since m_major and m_minor are 3 and 3, 7 or 8.
  std::vector<std::byte> clientProtocol = TextEncoding::toLatin1(getProtocolString());
  m_output->writeFully(clientProtocol.data(), clientProtocol.size());
  m_output->flush();
}

/**
 * Client send:
 * 2           - U8          - shared flag
 *
 * Server send: ServerInit (RfbParsing::readServerInit()).
 */
Task<> RemoteViewerCoreImpl::clientAndServerInit()
{
  if (m_sharedFlag) {
    m_logWriter.info(_T("Setting share flag in on..."));
  } else {
    m_logWriter.info(_T("Setting share flag is off..."));
  }
  m_output->writeUInt8(m_sharedFlag);
  m_output->flush();
  m_logWriter.debug(_T("Shared flag is set"));

  RfbParsing::ServerInit serverInit =
    co_await RfbParsing::readServerInit(m_reader, m_logWriter);
  {
    AutoLock al(&m_fbLock);
    setFbProperties(serverInit.dimension, serverInit.pixelFormat);
  }

  m_remoteDesktopName = serverInit.desktopName;
  m_logWriter.info(_T("Server remote name: %s"), m_remoteDesktopName.getString());

  if (m_isTight) {
    m_logWriter.detail(_T("Reading tight capabilities"));
    co_await readCapabilities();
  }
}

/**
 * Struct filled from the Capabilities message
 * 2  - U16         - nServerMessageTypes
 * 2  - U16         - nClientMessageTypes
 * 2  - U16         - nEncodingTypes
 * 2  - U16         - padding. Reserved, must be 0
 * followed by nServerMessageTypes * rfbCapabilityInfo structures
 * followed by nClientMessageTypes * rfbCapabilityInfo structures
 * followed by       nEncodingType * rfbCapabilityInfo structures
 */
Task<> RemoteViewerCoreImpl::readCapabilities()
{
  int nServerMessageTypes = co_await m_reader.readUInt16();
  int nClientMessageTypes = co_await m_reader.readUInt16();
  int nEncodingTypes = co_await m_reader.readUInt16();
  co_await m_reader.readUInt16(); //padding

  m_logWriter.detail(_T("Server message types (capability):"));
  while (nServerMessageTypes--) {
    RfbCapabilityInfo cap = co_await readCapability();
    m_serverMsgCaps.enable(cap);
  }

  m_logWriter.detail(_T("Client message types (capability):"));
  while (nClientMessageTypes--) {
    RfbCapabilityInfo cap = co_await readCapability();
    m_clientMsgCaps.enable(cap);
  }

  m_logWriter.detail(_T("Encodings (capability):"));
  while (nEncodingTypes--) {
    RfbCapabilityInfo cap = co_await readCapability();
    m_encodingCaps.enable(cap);
  }

  // Add active encoding-capabilities in DecoderStore.
  for (size_t i = 0; i < m_encodingCaps.numEnabled(); i++) {
    UINT32 code = m_encodingCaps.getByOrder(i);
    auto handler = m_decoderHandlers.find(code);
    if (handler != m_decoderHandlers.end() && handler->second) {
      m_decoderStore.addDecoder(std::move(handler->second), m_decoderPriority[code]);
    }
  }
}

Task<RfbCapabilityInfo> RemoteViewerCoreImpl::readCapability()
{
  // read capability
  RfbCapabilityInfo cap;
  cap.code = co_await m_reader.readUInt32();
  co_await m_reader.readFully(cap.vendorSignature, RfbCapabilityInfo::vendorSigSize);
  co_await m_reader.readFully(cap.nameSignature, RfbCapabilityInfo::nameSigSize);

  // transform information for the log and log him; the signatures are
  // ASCII, and not null-terminated when they fill their arrays.
  StringStorage vendorSignatureString = TextEncoding::fromLatin1(
    std::as_bytes(std::span(cap.vendorSignature, RfbCapabilityInfo::vendorSigSize)));
  StringStorage nameSignatureString = TextEncoding::fromLatin1(
    std::as_bytes(std::span(cap.nameSignature, RfbCapabilityInfo::nameSigSize)));

  m_logWriter.detail(_T("code: %u, vendor: %s, signature: %s"),
                     cap.code, vendorSignatureString.getString(), nameSignatureString.getString());

  // return capability info
  co_return cap;
}

void RemoteViewerCoreImpl::addAuthCapability(AuthHandler *authHandler,
                                         UINT32 code,
                                         const char *vendorSignature,
                                         const char *nameSignature,
                                         const StringStorage description)
{
  m_authCaps.add(code, vendorSignature, nameSignature, description);
  registerAuthHandler(code, authHandler);
}

void RemoteViewerCoreImpl::addServerMsgCapability(ServerMessageListener *listener,
                                              UINT32 code,
                                              const char *vendorSignature,
                                              const char *nameSignature,
                                              const StringStorage description)
{
  m_serverMsgCaps.add(code, vendorSignature, nameSignature, description);
  registerMessageListener(code, listener);
}

void RemoteViewerCoreImpl::addClientMsgCapability(UINT32 code,
                                              const char *vendorSignature,
                                              const char *nameSignature,
                                              const StringStorage description)
{
  m_clientMsgCaps.add(code, vendorSignature, nameSignature, description);
}

void RemoteViewerCoreImpl::addEncodingCapability(std::unique_ptr<Decoder> decoder,
                                             int priorityEncoding,
                                             UINT32 code,
                                             const char *vendorSignature,
                                             const char *nameSignature,
                                             const StringStorage description)
{
  m_encodingCaps.add(code, vendorSignature, nameSignature, description);
  registerDecoderHandler(code, std::move(decoder), priorityEncoding);
}

void RemoteViewerCoreImpl::getEnabledClientMsgCapabilities(std::vector<UINT32> *codes) const
{
  m_clientMsgCaps.getEnabledCapabilities(*codes);
}

void RemoteViewerCoreImpl::getEnabledServerMsgCapabilities(std::vector<UINT32> *codes) const
{
  m_serverMsgCaps.getEnabledCapabilities(*codes);
}


void RemoteViewerCoreImpl::registerAuthHandler(UINT32 code, AuthHandler *handler)
{
  m_authHandlers[code] = handler;
}

void RemoteViewerCoreImpl::registerMessageListener(UINT32 code, ServerMessageListener *listener)
{
  m_serverMsgHandlers[code] = listener;
}

void RemoteViewerCoreImpl::registerDecoderHandler(UINT32 code, std::unique_ptr<Decoder> decoder, int priority)
{
  m_decoderHandlers[code] = std::move(decoder);
  m_decoderPriority[code] = priority;
}

void RemoteViewerCoreImpl::sendChangedEncodings()
{
  sendEncodings();
  if (wasConnected()) {
    refreshFrameBuffer();
  }
}

void RemoteViewerCoreImpl::sendEncodings()
{
  // If core isn't connected, then m_output may be isn't initialized.
  // Exit from function, if it is.
  if (!wasConnected()) {
    return;
  }

  std::vector<INT32> decoderIds = m_decoderStore.getDecoderIds();
  RfbSetEncodingsClientMessage encodingsMessage(&decoderIds);
  encodingsMessage.send(*m_output);
}

void RemoteViewerCoreImpl::post(ClientEvent event)
{
  if (!wasStarted()) {
    dispatch(event);
    return;
  }
  postToLoop([this, event = std::move(event)] {
    try {
      dispatch(event);
    } catch (const Exception &e) {
      m_logWriter.error(_T("Cannot handle an event: %s"), e.getMessage());
    } catch (const std::exception &e) {
      m_logWriter.error(_T("Cannot handle an event: %hs"), e.what());
    }
  });
}

void RemoteViewerCoreImpl::dispatch(const ClientEvent &event)
{
  if (m_isFinished) {
    return;
  }
  // DecoderStore has no lock, and the protocol looks up a decoder for every
  // rectangle, and may be suspended in one, so changes to it wait for a
  // message boundary.
  if (Internal::changesDecoderStore(event) && !m_isAtMessageBoundary) {
    m_pendingEvents.push_back(event);
    return;
  }
  std::visit([this](const auto &e) { handle(e); }, event);
}

void RemoteViewerCoreImpl::applyPendingEvents()
{
  std::vector<ClientEvent> events;
  events.swap(m_pendingEvents);
  for (const ClientEvent &event : events) {
    std::visit([this](const auto &e) { handle(e); }, event);
  }
}

void RemoteViewerCoreImpl::handle(const PixelFormatEvent &event)
{
  const auto &[pixelFormat] = event;
  m_logWriter.detail(_T("Pixel format will changed"));
  m_isNewPixelFormat = true;
  m_viewerPixelFormat = pixelFormat;
}

void RemoteViewerCoreImpl::handle(const RefreshEvent &/* event */)
{
  m_logWriter.detail(_T("Frame buffer will refreshed"));
  m_isRefreshing = true;
}

void RemoteViewerCoreImpl::handle(const KeyEvent &event)
{
  // If core isn't connected, then m_output may be isn't initialized.
  // Exit from function, if it is.
  if (!wasConnected()) {
    return;
  }
  if (m_input != nullptr) {
    send(event);
    return;
  }
  m_inputEvents.push(event);
  m_writeWake->cancel();
}

void RemoteViewerCoreImpl::handle(const PointerEvent &event)
{
  if (!wasConnected()) {
    return;
  }
  // At once, even if the message waits: the local cursor follows the mouse.
  m_fbUpdateNotifier.updatePointerPos(event.position);
  if (m_input != nullptr) {
    send(event);
    return;
  }
  m_inputEvents.push(event);
  m_writeWake->cancel();
}

void RemoteViewerCoreImpl::handle(const CutTextEvent &event)
{
  if (!wasConnected()) {
    return;
  }
  if (m_input != nullptr) {
    send(event);
    return;
  }
  m_inputEvents.push(event);
  m_writeWake->cancel();
}

void RemoteViewerCoreImpl::send(const KeyEvent &event)
{
  const auto &[downFlag, key] = event;
  m_logWriter.detail(_T("Sending key event: %d, %u..."), downFlag, key);
  RfbKeyEventClientMessage keyMessage(downFlag, key);
  keyMessage.send(*m_output);
  m_logWriter.debug(_T("Key event: %d, %u is sent"), downFlag, key);
}

void RemoteViewerCoreImpl::send(const PointerEvent &event)
{
  const auto &[buttonMask, position] = event;
  m_logWriter.detail(_T("Sending pointer event 0x%X, (%d, %d)..."),
                     static_cast<int>(buttonMask), position.x, position.y);
  // send position to server
  RfbPointerEventClientMessage pointerMessage(buttonMask, position);
  pointerMessage.send(*m_output);

  m_logWriter.debug(_T("Pointer event: 0x%X, (%d, %d) is sent"),
                    static_cast<int>(buttonMask), position.x, position.y);
}

void RemoteViewerCoreImpl::send(const CutTextEvent &event)
{
  const auto &[cutText] = event;
  RfbCutTextEventClientMessage cutTextMessage(cutText);
  if (m_isUtf8ClipboardEnabled) {
    // The clipboard may hold passwords, so only its length is logged.
    m_logWriter.detail(_T("Sending UTF-8 clipboard cut text (%zu characters)..."),
                       cutText.getLength());
    cutTextMessage.sendUtf8(*m_output);
  }
  else {
    m_logWriter.detail(_T("Sending clipboard cut text (%zu characters)..."),
                       cutText.getLength());
    cutTextMessage.send(*m_output);
  }
  m_logWriter.debug(_T("Clipboard cut text is sent"));
}

void RemoteViewerCoreImpl::handle(const PreferredEncodingEvent &event)
{
  const auto &[encodingType] = event;
  // Only if it changed: see DecoderStore::setPreferredEncoding().
  if (m_decoderStore.setPreferredEncoding(encodingType)) {
    sendChangedEncodings();
  }
}

void RemoteViewerCoreImpl::handle(const CopyRectEvent &event)
{
  const auto &[allow] = event;
  // Only if it changed: see DecoderStore::setPreferredEncoding().
  if (m_decoderStore.allowCopyRect(allow)) {
    sendChangedEncodings();
  }
}

void RemoteViewerCoreImpl::handle(const CompressionLevelEvent &event)
{
  const auto &[newLevel] = event;
  bool needUpdate = false;
  for (int level = CompressionLevel::COMPRESSION_LEVEL_MIN;
       level <= CompressionLevel::COMPRESSION_LEVEL_MAX;
       level++)
    if (level != newLevel)
      needUpdate |= m_decoderStore.removeDecoder(CompressionLevel(&m_logWriter, level).getCode());

  // new compression level is valid?
  if (newLevel < CompressionLevel::COMPRESSION_LEVEL_MIN ||
      newLevel > CompressionLevel::COMPRESSION_LEVEL_MAX)
    return;

  needUpdate |= m_decoderStore.addDecoder(std::make_unique<CompressionLevel>(&m_logWriter, newLevel), -1);
  if (needUpdate) {
    sendChangedEncodings();
  }
}

void RemoteViewerCoreImpl::handle(const JpegQualityEvent &event)
{
  const auto &[newLevel] = event;
  bool needUpdate = false;
  for (int level = JpegQualityLevel::JPEG_QUALITY_LEVEL_MIN;
       level <= JpegQualityLevel::JPEG_QUALITY_LEVEL_MAX;
       level++)
    if (level != newLevel)
      needUpdate |= m_decoderStore.removeDecoder(JpegQualityLevel(&m_logWriter, level).getCode());

  // new jpeg quality level is valid?
  if (newLevel < JpegQualityLevel::JPEG_QUALITY_LEVEL_MIN ||
      newLevel > JpegQualityLevel::JPEG_QUALITY_LEVEL_MAX)
    return;

  needUpdate |= m_decoderStore.addDecoder(std::make_unique<JpegQualityLevel>(&m_logWriter, newLevel), -1);
  if (needUpdate) {
    sendChangedEncodings();
  }
}

void RemoteViewerCoreImpl::handle(const CursorShapeUpdatesEvent &event)
{
  const auto &[enabled] = event;
  bool needUpdate = false;
  if (enabled) {
    needUpdate |= m_decoderStore.addDecoder(std::make_unique<RichCursorDecoder>(&m_logWriter), -1);
    needUpdate |= m_decoderStore.addDecoder(std::make_unique<PointerPosDecoder>(&m_logWriter), -1);
  } else {
    needUpdate |= m_decoderStore.removeDecoder(PseudoEncDefs::RICH_CURSOR);
    needUpdate |= m_decoderStore.removeDecoder(PseudoEncDefs::POINTER_POS);
  }
  
  if (needUpdate) {
    sendChangedEncodings();
  }
}

void RemoteViewerCoreImpl::handle(const IgnoreCursorShapeUpdatesEvent &event)
{
  const auto &[ignored] = event;
  m_fbUpdateNotifier.setIgnoreShapeUpdates(ignored);
}

void RemoteViewerCoreImpl::handle(const StopUpdatingEvent &event)
{
  const auto &[isStopped] = event;
  if (isStopped == m_isFreeze)
    return;
  m_isFreeze = isStopped;
  if (!isStopped) {
    m_logWriter.detail(_T("Sending of frame buffer update request..."));
    sendFbUpdateRequest(!m_forceFullUpdate);
  }
}

void RemoteViewerCoreImpl::copyFrameBuffer(FrameBuffer &dst)
{
  AutoLock al(&m_fbLock);
  if (!dst.clone(m_frameBuffer)) {
    throw Exception(_T("Failed to copy the frame buffer"));
  }
}
