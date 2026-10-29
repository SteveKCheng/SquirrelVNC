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

#ifndef _REMOTE_VIEWER_CORE_IMPL_H_
#define _REMOTE_VIEWER_CORE_IMPL_H_

#include "log-writer/LogWriter.h"
#include "rfb/FrameBuffer.h"
#include "region/Dimension.h"
#include "region/Point.h"

#include "CapabilitiesManager.h"
#include "CapsContainer.h"
#include "CoreEventsAdapter.h"
#include "DecoderStore.h"
#include "io-lib/AwaitableReader.h"
#include "io-lib/DataOutputStream.h"
#include "io-lib/InputStream.h"
#include "io-lib/OutputStream.h"
#include "util/Task.h"
#include "ClientEvents.h"
#include "FbUpdateNotifier.h"
#include "QueuedOutputStream.h"
#include "ServerMessageListener.h"
#include "ViewerBufferSize.h"

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>

#include <atomic>
#include <functional>
#include <exception>
#include <future>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <variant>

//
// The implementation of RemoteViewerCore, which owns one and forwards each
// of its functions to it: see RemoteViewerCore.h for the interface, its
// threads, and the documentation of each public function here. For
// viewer-core only.
//
// It is also the CapabilitiesManager that RemoteViewerCore gives out
// (RemoteViewerCore::getCapabilitiesManager()).
//
class RemoteViewerCoreImpl final : public CapabilitiesManager
{
public:
  //
  // The functions of RemoteViewerCore (see there).
  //
  RemoteViewerCoreImpl(asio::any_io_executor executor, Logger *logger);

  RemoteViewerCoreImpl(const RemoteViewerCoreImpl &) = delete;
  RemoteViewerCoreImpl &operator=(const RemoteViewerCoreImpl &) = delete;
  RemoteViewerCoreImpl(RemoteViewerCoreImpl &&) = delete;
  RemoteViewerCoreImpl &operator=(RemoteViewerCoreImpl &&) = delete;

  ~RemoteViewerCoreImpl() override;

  void start(InputStream *input, OutputStream *output,
             std::shared_ptr<CoreEventsAdapter> adapter,
             bool sharedFlag,
             UINT64 maxFrameBufferPixels);

  void start(const TCHAR *host, UINT16 port,
             std::shared_ptr<CoreEventsAdapter> adapter,
             bool sharedFlag,
             UINT64 maxFrameBufferPixels);

  void start(asio::ip::tcp::socket socket,
             std::shared_ptr<CoreEventsAdapter> adapter,
             bool sharedFlag,
             UINT64 maxFrameBufferPixels);

  void stop();

  void setPixelFormat(const PixelFormat &viewerPixelFormat);

  void stopUpdating(bool isStopped);

  void refreshFrameBuffer();

  void sendKeyboardEvent(bool downFlag, UINT32 key);

  void sendPointerEvent(UINT8 buttonMask, const Point &position);

  void sendCutTextEvent(const StringStorage &cutText);

  void setPreferredEncoding(INT32 encodingType);

  void allowCopyRect(bool allow);

  void setJpegQualityLevel(int newJpegQualityLevel);

  void setCompressionLevel(int newCompressionLevel);

  void enableCursorShapeUpdates(bool enabled);

  void ignoreCursorShapeUpdates(bool ignored);

  void copyFrameBuffer(FrameBuffer &dst);

private:
  //
  // CapabilitiesManager, given out by getCapabilitiesManager(); see there
  // for each. postToLoop() takes the reference from m_adapterRef; every
  // post for this object goes through it.
  //
  void addAuthCapability(AuthHandler *authHandler,
                         UINT32 code,
                         const char *vendorSignature,
                         const char *nameSignature,
                         const StringStorage description = _T("")) override;

  void addServerMsgCapability(ServerMessageListener *listener,
                              UINT32 code,
                              const char *vendorSignature,
                              const char *nameSignature,
                              const StringStorage description = _T("")) override;

  void addClientMsgCapability(UINT32 code,
                              const char *vendorSignature,
                              const char *nameSignature,
                              const StringStorage description = _T("")) override;

  void addEncodingCapability(std::unique_ptr<Decoder> decoder,
                             int priorityEncoding,
                             UINT32 code,
                             const char *vendorSignature,
                             const char *nameSignature,
                             const StringStorage description = _T("")) override;

  void postToLoop(std::function<void()> work) override;

  std::shared_ptr<void> claimOwner() override;

  //
  // The codes of the client-to-server and server-to-client messages that
  // both sides support, for ConnectionInfo.
  //
  // Pre-conditions: network thread (CapsContainer is not synchronized).
  //
  void getEnabledClientMsgCapabilities(std::vector<UINT32> *codes) const;
  void getEnabledServerMsgCapabilities(std::vector<UINT32> *codes) const;

  //
  // Whether start() has been called: whether the connection's state now
  // belongs to the network thread. Stays true after stop(), or after the
  // connection has ended, as it only records that start() was called.
  // Consumer thread only, like the public functions that call it.
  //
  bool wasStarted() const;

  //
  // The protocol version in use, as the version message gives it:
  // "RFB 003.008\n" and so on. Meaningful once the core thread has read the
  // server's version, in handshake().
  //
  StringStorage getProtocolString() const;

  //
  // If the server announced the UTF8CUTT capability, allows sending
  // ClientCutTextUtf8 messages. If it announced UTF8CUTT and UTF8CUTE, sends
  // the EnableCutTextUtf8 message. Called on the core thread, after the
  // protocol initialization.
  //
  void allowUtf8Clipboard();

  //
  // The events of the public methods (ClientEvents.h).
  //
  using ClientEvent = Internal::ClientEvent;
  using KeyEvent = Internal::KeyEvent;
  using PointerEvent = Internal::PointerEvent;
  using CutTextEvent = Internal::CutTextEvent;
  using StopUpdatingEvent = Internal::StopUpdatingEvent;
  using RefreshEvent = Internal::RefreshEvent;
  using PixelFormatEvent = Internal::PixelFormatEvent;
  using PreferredEncodingEvent = Internal::PreferredEncodingEvent;
  using CopyRectEvent = Internal::CopyRectEvent;
  using JpegQualityEvent = Internal::JpegQualityEvent;
  using CompressionLevelEvent = Internal::CompressionLevelEvent;
  using CursorShapeUpdatesEvent = Internal::CursorShapeUpdatesEvent;
  using IgnoreCursorShapeUpdatesEvent = Internal::IgnoreCursorShapeUpdatesEvent;

  //
  // Hands event to dispatch(): on the network thread (postToLoop()) once
  // start() has been called, so that the events are handled in the order
  // they were posted, between the steps of the protocol; before start(), at
  // once, on the calling thread, as nothing runs yet. An exception from a
  // handler on the network thread is logged, and the event dropped.
  //
  // Pre-conditions: the consumer thread (see the class).
  // Throws what the handler throws, before start().
  //
  void post(ClientEvent event);

  //
  // Handles event with its handle() function, except that the events that
  // change m_decoderStore (Internal::changesDecoderStore()) are queued for
  // applyPendingEvents(), unless the protocol is between messages
  // (m_isAtMessageBoundary). Does nothing once the connection has finished
  // (m_isFinished). On the network thread, or before start().
  //
  void dispatch(const ClientEvent &event);

  //
  // Handles the events queued by dispatch(), in the order they were posted.
  // Called on the network thread, by the protocol, where no decoder is in
  // use: before each server message is read, and once after the protocol
  // initialization.
  //
  // Post-conditions: the events queued when it was called have been
  // handled, unless a handler threw; the events after that one are dropped.
  // Throws what the handlers throw.
  //
  void applyPendingEvents();

  //
  // The handlers of post(), one for each public method: see that method for
  // what each does, and what it may throw. Input (KeyEvent, PointerEvent,
  // CutTextEvent) is dropped until the protocol is initialized; then, for a
  // socket, it is queued in m_inputEvents for writeLoop(), which sends it
  // with send() when the link allows, and the pointer position is given to
  // the frame buffer notifier at once; with streams, it is sent at once.
  //
  void handle(const KeyEvent &event);
  void handle(const PointerEvent &event);
  void handle(const CutTextEvent &event);
  void handle(const StopUpdatingEvent &event);
  void handle(const RefreshEvent &event);
  void handle(const PixelFormatEvent &event);
  void handle(const PreferredEncodingEvent &event);
  void handle(const CopyRectEvent &event);
  void handle(const JpegQualityEvent &event);
  void handle(const CompressionLevelEvent &event);
  void handle(const CursorShapeUpdatesEvent &event);
  void handle(const IgnoreCursorShapeUpdatesEvent &event);

  //
  // Encode input events into m_output: the client messages KeyEvent,
  // PointerEvent and ClientCutText (or ClientCutTextUtf8). On the network
  // thread, once the protocol is initialized.
  //
  void send(const KeyEvent &event);
  void send(const PointerEvent &event);
  void send(const CutTextEvent &event);

  //
  // The connection, on the network thread: an Asio coroutine, spawned by
  // start() and bound to m_stopSignal. It connects (or takes over the
  // socket given to start()), then runs readLoop() and writeLoop() until
  // the first of them ends; or, with streams, runs run() with runBlocking().
  // It ends the output queue before it returns.
  //
  // Throws what run() threw, or SocketException if the host cannot be
  // resolved or connected to; finish() reports it to the adapter.
  //
  asio::awaitable<void> drive();

  //
  // Resolves m_host (asyncResolve()) and connects m_socket to the first
  // address that accepts; or uses m_acceptedSocket. Then sets the
  // socket's options (configureSocket()). Throws SocketException on failure.
  //
  asio::awaitable<void> connect();

  //
  // Turns off Nagle's algorithm (TCP_NODELAY): RFB's small messages, such as
  // pointer events, must not wait for earlier data to be acknowledged. Turns
  // on TCP keepalive (KEEPALIVE_*), so that a peer gone without closing the
  // connection is noticed: a view-only connection sends nothing, and would
  // otherwise wait for an update forever. A failure is logged, and the
  // connection goes on.
  //
  void configureSocket();

  //
  // The driver of the protocol (notes/async-networking-resumption.md): runs
  // run(), reading from m_socket whenever m_reader runs short, and serving
  // its requests (serveRequest()). Ends when run() has finished, its
  // exception kept in m_runError; a read error, a failed request, or a
  // cancellation, is given to m_reader, so that the protocol code unwinds.
  //
  asio::awaitable<void> readLoop();

  //
  // Serves a request of the protocol code, such as the password, without
  // blocking the network thread: starts it (Request::start()) and waits
  // until it is done, on whichever thread. A cancellation of the driver
  // (stop()) cancels the request (Request::cancel()), which is still
  // waited for, so that no request is left to complete after the end.
  //
  // Returns the exception the request failed with, or null.
  //
  asio::awaitable<std::exception_ptr> serveRequest(AwaitableReader::Request &request);

  //
  // Sends what m_output queues (m_queuedOutput), whenever it is woken (by a
  // flush, or by input to send), until
  // cancelled or a send fails; a failure is given to m_reader as an
  // IOException, so that the protocol code ends too. Before each send, it
  // encodes the input queued meanwhile (m_inputEvents), so that pointer
  // moves are coalesced while a send is in progress.
  //
  asio::awaitable<void> writeLoop();

  //
  // Reports how the connection ended to the adapter, as onAuthError(),
  // onDisconnect() or onError(), and sets m_isFinished. The completion of
  // drive(), on the network thread.
  //
  void finish(std::exception_ptr failure);

  //
  // The protocol, from the handshake to the end of the session, as a
  // coroutine (util/Task.h) that reads through m_reader. drive() connects,
  // then runs it, and finish() reports how it ended to the adapter.
  //
  // Pre-conditions: connected; m_reader has no bytes yet.
  // Post-conditions: when it returns, the session has ended normally, and
  // the adapter has been told with onDisconnect().
  // Throws what the protocol throws: AuthException, IOException when the
  // connection fails, or Exception.
  //
  Task<> run();

  //
  // Initialization, called by the constructor.
  //
  void init();

  //
  // This one includes common code reused by public start(...) functions.
  //
  void start(std::shared_ptr<CoreEventsAdapter> adapter, bool sharedFlag,
             UINT64 maxFrameBufferPixels);

  //
  // RFB protocol implementation.
  //

  //
  // This method processes FramebufferUpdate server message (code 0):
  //   * receive the number of rectangles in this update,
  //   * then call receiveFrameBufferUpdRectangle() for each rectangle,
  //   * then send FramebufferUpdateRequest client message (code 3).
  //
  Task<> receiveFbUpdate();

  //
  // Receive rectangle, decode it and notify m_fbUpdateNotifier.
  //
  // Returns true if this rectangle should be the last one in this update,
  // false otherwise. This is needed to support LastRect pseudo-encoding
  // (code -224).
  //
  Task<bool> receiveFbUpdateRectangle();

  //
  // Process a fake rectangle which represents a pseudo-encoding.
  //
  // Pre-conditions: rect has been read from the update header, and the
  // input is at the rectangle's data.
  // Post-conditions: the data has been read, and the frame buffer, the
  // cursor or the pointer position updated.
  // Throws Exception for an unsupported pseudo-encoding, a cursor wider or
  // taller than ViewerBufferSize::MAX_CURSOR_SIZE, or a desktop size that
  // setFbProperties() rejects; IOException if the input fails.
  //
  // A coroutine (util/Task.h): it awaits its reads from reader, and takes
  // m_fbLock only where it does not read.
  //
  Task<> processPseudoEncoding(const Rect &rect, int encType);

  //
  // Send FramebufferUpdateRequest client message (code 3).
  // This method updates pixel format if needed.
  //
  void sendFbUpdateRequest(bool incremental = true);

  //
  // Receive Bell server message (code 2) and send event to the adapter.
  //
  void receiveBell();

  //
  // Reads a reason string: a 32-bit length, then that many bytes of text.
  //
  // Post-conditions: returns the text decoded by
  // TextEncoding::fromRfbText(): UTF-8 if valid, otherwise Latin-1.
  // Throws IOException if the length is over MAX_REASON_LENGTH, leaving
  // the text unread; otherwise whatever reading the input throws.
  //
  Task<StringStorage> readReason();

  //
  // Receive ServerCutText server message (code 3) and send event to the
  // adapter.
  //
  // Pre-conditions: the message type has been read.
  // Post-conditions: the message has been read whole, and its text sent
  // to the adapter, decoded by TextEncoding::fromRfbText(): Latin-1 as
  // the specification says, or UTF-8 if it is valid UTF-8. The text ends
  // at the first zero byte; text longer than 64 MiB is skipped with a
  // warning (RfbParsing::readServerCutText()). Exceptions from the adapter are logged, not
  // passed on. Throws whatever reading the input throws.
  //
  Task<> receiveServerCutText();

  //
  // Receive TightVNC's ServerCutTextUtf8 message (code 0xfc000200) and
  // send its text, decoded as UTF-8, to the adapter.
  //
  // Pre-conditions: the message type has been read.
  // Post-conditions: the message has been read whole. Empty text is
  // ignored; text longer than 64 MiB is skipped with a warning
  // (RfbParsing::readServerCutTextUtf8()). Invalid UTF-8 becomes U+FFFD, and the text ends at the first
  // zero byte. Exceptions from the adapter are logged, not passed on.
  // Throws whatever reading the input throws.
  //
  Task<> receiveServerCutTextUtf8();

  bool isRfbProtocolString(const char protocol[12]) const;
  Task<> handshake();
  Task<int> negotiateSecurityType();
  Task<> authenticate();
  Task<> clientAndServerInit();
  Task<> readSecurityTypeList(std::vector<UINT32> *secTypes);
  StringStorage getSecurityTypeName(UINT32 securityType) const;
  StringStorage getAuthenticationTypeName(UINT32 authenticationType) const;
  //
  // Returns the security type to use, from those the server offers in
  // secTypes: TIGHT if isTightEnabled and it is offered, or else the first
  // type that has a handler in authHandlers or is NONE.  The result is
  // therefore one of the types this program knows.
  // @throw Exception if no type in secTypes qualifies.
  //
  int selectSecurityType(const std::vector<UINT32> &secTypes,
                         const std::map<UINT32, AuthHandler *> &authHandlers,
                         const bool isTightEnabled) const;
  Task<> initTunnelling();
  Task<int> initAuthentication();
  Task<> readCapabilities();
  Task<RfbCapabilityInfo> readCapability();
  void sendEncodings();

  //
  // Sends SetEncodings after a setting changed them during the session,
  // then makes the next update request ask for the whole screen
  // (refreshFrameBuffer()). A server may drop the frame it is encoding when
  // the encodings change, without sending its areas again later: neatvnc
  // does, unless the client supports Fence (finish_fb_update()). Before the
  // connection, only as sendEncodings().
  //
  void sendChangedEncodings();

  //
  // This function return true, if flag m_wasConnected is true.
  // This flag is true after onConnected().
  // Pre-conditions: network thread, or before start().
  //
  bool wasConnected() const;

  //
  // Update properties (Dimension and PixelfFormat) of m_frameBuffer.
  //
  // Pre-conditions: m_fbLock is held.
  // Post-conditions: m_frameBuffer has the given size and format, filled
  // with black.
  // Throws Exception before changing the frame buffer if the size is
  // negative, has more pixels than m_maxFrameBufferPixels or does not fit
  // in memory (ViewerBufferSize::fitsFrameBuffer()), or the format is not
  // valid (PixelFormat::isValid()); and Exception if a buffer cannot be
  // set up.
  //
  void setFbProperties(const Dimension &fbDimension,
                       const PixelFormat &fbPixelFormat);

  //
  // If m_isNewPixelFormat flag is set to true, then pixel format of the frame buffer
  // will be updated to m_viewerPixelFormat.
  //
  // This method must be called only from the input thread, otherwise data corruption
  // is possible on the protocol level.
  //
  // Returns true if pixel format was actually changed.
  //
  // The first SetPixelFormat of the connection is sent even if the format
  // equals that of the frame buffer, because that may be a stand-in for a
  // server format without true colour (see RfbParsing::readPixelFormat()).
  // Throws Exception if m_viewerPixelFormat is not valid
  // (PixelFormat::isValid()); nothing is sent then.
  //
  bool updatePixelFormat();

  //
  // This method add pair <code, handler> to map m_authHandler.
  //
  void registerAuthHandler(const UINT32 code, AuthHandler *handler);

  //
  // This method add pair <code, listener> to map m_serverMsgHandlers.
  //
  void registerMessageListener(const UINT32 code, ServerMessageListener *listener);

  //
  // This method add pair <code, decoder> to map m_decoderHandlers.
  //
  void registerDecoderHandler(const UINT32 code, std::unique_ptr<Decoder> decoder, int priority);

  LogWriter m_logWriter;

  // TCP keepalive (configureSocket()): a dead peer is noticed after about
  // 30 + 10 * 5 = 80 seconds without traffic.
  static constexpr DWORD KEEPALIVE_IDLE_SECONDS = 30;
  static constexpr DWORD KEEPALIVE_INTERVAL_SECONDS = 5;
  static constexpr DWORD KEEPALIVE_PROBE_COUNT = 10;

  // What start() was given: a host and port to connect to; a connected
  // socket (empty if none, and once connect() has moved it to m_socket);
  // or streams (m_input not null).
  StringStorage m_host;
  UINT16 m_port;
  std::optional<asio::ip::tcp::socket> m_acceptedSocket;

  // The network thread's executor, for the connection: given to the
  // constructor.
  asio::any_io_executor m_executor;
  // The connection's socket, made by drive(); used only on the network
  // thread.
  std::optional<asio::ip::tcp::socket> m_socket;
  // Wakes writeLoop(), by being cancelled; used only on the network thread.
  std::optional<asio::steady_timer> m_writeWake;
  // Cancels drive(); emitted on the network thread, by stop().
  asio::cancellation_signal m_stopSignal;
  // Set by stop(): the message loop of run() ends.
  std::atomic<bool> m_isStopping;
  // What left run(), for drive() to rethrow.
  std::exception_ptr m_runError;
  // Set by finish(): drive() has completed, and m_stopSignal must not be
  // emitted any more. Used only on the network thread.
  bool m_isFinished;
  // Set while run() waits for the type of the next server message: no
  // decoder is in use then, so dispatch() applies changes to the decoder
  // set at once. Used only on the network thread.
  bool m_isAtMessageBoundary;

  // With streams, the input stream given to start(), read with
  // runBlocking(); otherwise null.
  InputStream *m_input;
  // What the coroutines of the protocol (run() and those it awaits) read;
  // readLoop() feeds it. Used only on the network thread.
  AwaitableReader m_reader;
  // What the protocol writes, made by start(): over m_queuedOutput for a
  // socket; with streams, over the output stream given to start(). Null
  // before start(). Used only on the network thread.
  std::unique_ptr<DataOutputStream> m_output;
  QueuedOutputStream m_queuedOutput;

  // The adapter given to start(). The connection holds a reference to it
  // while it runs, and each piece of posted work while it waits (see
  // start()); m_adapterRef is where postToLoop() takes them from.
  CoreEventsAdapter *m_adapter;
  std::weak_ptr<CoreEventsAdapter> m_adapterRef;

  // m_decoderStore depends on m_logWriter and must be defined after it.
  // See also: C++ standard 12.6.2 - Initializing bases and members.
  DecoderStore m_decoderStore;

  // m_fbUpdateNotifier depends on m_logWriter and must be defined after it.
  // See also: C++ standard 12.6.2 - Initializing bases and members.
  FbUpdateNotifier m_fbUpdateNotifier;

  CapsContainer m_authCaps;
  std::map<UINT32, AuthHandler *> m_authHandlers;

  CapsContainer m_clientMsgCaps;
  CapsContainer m_serverMsgCaps;
  std::map<UINT32, ServerMessageListener *> m_serverMsgHandlers;

  CapsContainer m_encodingCaps;
  // Moved to m_decoderStore when the server enables the encoding.
  std::map<UINT32, std::unique_ptr<Decoder> > m_decoderHandlers;
  std::map<UINT32, int> m_decoderPriority;

  // The events queued by dispatch() for applyPendingEvents(). Used before
  // start() on the thread that calls it, then only on the network thread.
  std::vector<ClientEvent> m_pendingEvents;
  // The input waiting for writeLoop(). Used only on the network thread.
  Internal::InputEventQueue m_inputEvents;
  // Set by start(). Consumer thread only (see wasStarted()).
  bool m_wasStarted;

  // The frame buffer limit, in pixels. Set by start(), before the network
  // thread reads it.
  UINT64 m_maxFrameBufferPixels;

  // This flag is set after onConnected(). Network thread only (or, before
  // start(), the thread that calls it), like the fields below that have no
  // lock: their locks went when every use moved to the network thread
  // (notes/async-networking.md, Phase 4 step 7).
  bool m_wasConnected;

  // This is general frame buffer of RemoteViewerCoreImpl and local mutex to change him.
  // This frame buffer contain actual state of remote desktop.
  // Cursor painted on him before calling CoreEventsAdapter::onFrameBufferUpdate()
  // and erased after (thread FbUpdateNotifier).
  //
  // Mutex m_fbLock must locked into only this thread, else may be deadlock.
  LocalMutex m_fbLock;
  FrameBuffer m_frameBuffer;

  Dimension m_desktopSize;

  bool m_isNewPixelFormat;
  PixelFormat m_viewerPixelFormat;
  // True once SetPixelFormat has been sent. Protected by m_fbLock.
  bool m_isPixelFormatSent;

  bool m_isRefreshing;

  bool m_isFreeze;

  bool m_isNeedRequestUpdate;

  bool m_sharedFlag;
  int m_major;
  int m_minor;
  bool m_isTightEnabled;
  bool m_isTight;
  StringStorage m_remoteDesktopName;
  bool m_isUtf8ClipboardEnabled;

  bool m_forceFullUpdate;
};

#endif
