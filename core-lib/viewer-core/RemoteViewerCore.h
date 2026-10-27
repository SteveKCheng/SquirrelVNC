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

#ifndef _REMOTE_VIEWER_CORE_H_
#define _REMOTE_VIEWER_CORE_H_

#include "log-writer/LogWriter.h"
#include "network/RfbInputGate.h"
#include "network/RfbOutputGate.h"
#include "rfb/FrameBuffer.h"
#include "region/Dimension.h"
#include "region/Point.h"

#include "CapsContainer.h"
#include "CoreEventsAdapter.h"
#include "DecoderStore.h"
#include "io-lib/AwaitableReader.h"
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
// RemoteViewerCore implements a local representation of a live remote screen
// updated in real time via the RFB protocol. It maintains screen data in its
// own frame buffer and notifies the target class (the adapter) about various
// events like changes in the frame buffer, incoming clipboard transfers from
// the server, protocol state changes and so on.
//
// Also, it implements remote control feature by passing arbitrary keyboard
// and pointer events to the remote computer.
//
// The interface of this class hides all the complexity of the RFB protocol
// and provides the caller with an easy way to maintain an RFB connection and
// get access to the screen data.
//
// Threads: the public functions are for one thread, the consumer's (the
// GUI thread in the viewer; the main thread in rfb-replay, whose tests take
// the GUI's role), and not for the adapter's callbacks, which come on the
// network thread. Before start(), the settings change this object
// directly; from start() on, they are posted to the network thread, which
// owns the connection's state. The exceptions are the capability
// functions, postToLoop() and claimOwner(), which have their own thread
// rules (see there).
//
// Each function can be called on any stage of the protocol, unless
// documented otherwise.
//
// Finally, each function of this class may throw exceptions, unless it's
// explicitly stated that it will never do so.
//
class RemoteViewerCore final : public CapabilitiesManager
{
public:
  //
  // Creates an object that does no protocol work until start() is called.
  // The connection will run on executor: in the viewer, that of its
  // NetworkThread, a loop that every connection shares.
  //
  // To allow logging, pass a pointer to an object which implements the Logger
  // interface (see log-writer/Logger.h). Logging is disabled by default.
  //
  // Pre-conditions: the execution context of executor (the NetworkThread's
  // thread pool) outlives this object and the work it posts there; a
  // NetworkThread waits for that work when it is destroyed.
  //
  RemoteViewerCore(asio::any_io_executor executor, Logger *logger = nullptr);

  RemoteViewerCore(const RemoteViewerCore &) = delete;
  RemoteViewerCore &operator=(const RemoteViewerCore &) = delete;
  RemoteViewerCore(RemoteViewerCore &&) = delete;
  RemoteViewerCore &operator=(RemoteViewerCore &&) = delete;

  //
  // Ends the frame buffer notifier's thread, waiting for it, and closes a
  // socket taken over by start() but never used. Never throws.
  //
  // Pre-conditions: if start() was called, the connection has released
  // every reference to the adapter it was given (see start()): the object
  // is destroyed with the adapter's owner, once nothing else holds that.
  //
  ~RemoteViewerCore() override;

  //
  // Each overloaded start() function actually starts operation.
  //
  // All start() functions attach the object to the specified data connection
  // (e.g. an open TCP/IP socket), set callbacks for events originating in the
  // object, and actually start operation.
  //
  // All three start() functions work similarly, but may do different work
  // at startup. For example, one of them initiates new TCP connection.
  //
  // These functions start the connection on the network thread
  // (the executor given to the constructor) and return control to the
  // calling thread. Only one of these functions should be called, and it
  // should be called just once.
  //
  // maxFrameBufferPixels is the largest frame buffer the server may ask
  // for, in pixels (width * height), in ServerInit or in DesktopSize
  // updates; a larger one ends the session with an Exception, reported
  // through CoreEventsAdapter::onError().
  //
  // On various events, corresponding callback functions will be called via
  // the CoreEventsAdapter interface (the adapter). To receive these
  // notifications, the application should inherit CoreEventsAdapter and
  // provide a pointer to that subclass to one of the start() functions.
  //
  // Ownership (notes/async-networking.md, "Ownership by std::shared_ptr"):
  // the adapter's owner must keep this object, and everything registered
  // with it (authentication handlers, capabilities), alive as long as it
  // holds a reference; typically they are all parts of one object, and the
  // adapter a std::shared_ptr made with the aliasing constructor, sharing
  // that object's control block. The connection keeps a reference while it
  // runs, and each piece of work posted for this object keeps one until it
  // has run, so the owner is not deleted before the network thread is done
  // with this object, without anyone waiting. The last of these references
  // is released after the adapter has been told how the connection ended,
  // on the network thread, so the owner may be deleted there: an owner
  // that must be deleted on another thread needs a deleter that sees to it.
  //
  // The threading model is the following. The connection is driven on the
  // network thread (notes/async-networking.md, Phase 4): connecting, reading
  // with the protocol code, and writing. This thread is referred to as the
  // "input thread" below. It also performs most notifications via the
  // adapter interface, except for two notifications which report changes in
  // the frame buffer.
  //
  // The notifications related to the frame buffer, onFrameBufferUpdate() and
  // onFrameBufferPropChange(), will be called from a separate thread (let's
  // call it "frame buffer notifier"). The whole purpose of this thread is to
  // perform these two callbacks. This architecture allows input thread to
  // continue reading network data while callbacks are executed. However, note
  // that the frame buffer is locked in these two callbacks, so it's still
  // possible to block the input thread by doing too much work in the
  // callbacks -- obviously, the input thread will be blocked as soon as it
  // needs to change the frame buffer.
  //
  // Write operations (sending data to the server) are performed on the
  // network thread only, so the RfbOutputGate has no lock: the consumer
  // posts events (sendKeyboardEvent() and the like), which the network
  // thread encodes. For a socket, flushing only queues the message
  // (QueuedOutputStream), and the writer sends it.
  //

  //
  // This version of start() takes any type of connection that can be
  // specified via a pair of abstract "gates" (RfbInputGate and RfbOutputGate).
  // The input is read with blocking reads on the network thread, which no
  // other connection could share then: it is meant for input in memory, for
  // tests (rfb-replay).
  //
  void start(RfbInputGate *input, RfbOutputGate *output,
             std::shared_ptr<CoreEventsAdapter> adapter,
             bool sharedFlag = true,
             UINT64 maxFrameBufferPixels = ViewerBufferSize::DEFAULT_MAX_FRAME_BUFFER_PIXELS);

  //
  // This version initiates new TCP connection to the specified host and port,
  // and then uses the established connection for data exchange. (Asio
  // initializes Windows sockets itself.)
  //
  void start(const TCHAR *host, UINT16 port,
             std::shared_ptr<CoreEventsAdapter> adapter,
             bool sharedFlag = true,
             UINT64 maxFrameBufferPixels = ViewerBufferSize::DEFAULT_MAX_FRAME_BUFFER_PIXELS);

  //
  // This version expects a connected TCP socket. For example, it can be used
  // for reverse connections (when servers connect to viewers): the socket
  // accepted by a TcpAcceptor. The core closes it.
  //
  // Pre-conditions: socket is open, and belongs to the executor given to
  // the constructor (it was made, or accepted, on the network thread), with
  // no operation pending.
  //
  void start(asio::ip::tcp::socket socket,
             std::shared_ptr<CoreEventsAdapter> adapter,
             bool sharedFlag = true,
             UINT64 maxFrameBufferPixels = ViewerBufferSize::DEFAULT_MAX_FRAME_BUFFER_PIXELS);

  //
  // Stop operation: cancels the connection, on the network thread, whatever
  // it is doing (resolving, connecting, reading or writing), and ends the
  // frame buffer notifier's thread.
  //
  // This function will return control to the caller immediately, and the
  // connection may run for some time after the function has finished. It
  // releases its references to the adapter once it has ended (see start()).
  //
  // The socket, connected here or taken over, is closed when the connection
  // ends; gates given to start() are not closed.
  //
  void stop();

  //
  // Set the specified pixel format. The viewer will request that pixel format
  // from the server, as well as a full screen update. The pixel format is not
  // guaranteed to be applied immediately. It will be applied when allowed by
  // the protocol, and adapter's onFrameBufferPropChange() will be called after
  // that.
  //
  // Timing: the format is applied by updatePixelFormat() in the next
  // sendFbUpdateRequest(), which sends SetPixelFormat ahead of that request.
  // - Called before start(), as ViewerWindow does through
  //   setRemoteViewerCore(), it is applied with the first update request.
  //   (ViewerWindow calls it again when it handles the connection; the
  //   format is the same, so nothing more is sent.)
  // - Called later, when the user changes the colour setting, it is posted
  //   to the network thread (post()), and applied with the first update
  //   request sent after it is handled there; which request that is
  //   depends on when the server's messages arrive.
  //
  void setPixelFormat(const PixelFormat &viewerPixelFormat);

  //
  // Pause/resume updating the frame buffer.
  //
  void stopUpdating(bool isStopped);

  //
  // Request full refresh of the framebuffer, so that the whole screen will be
  // re-sent from the server. The refresh is not guaranteed to happen
  // immediately, it will be completed when allowed by the protocol: the
  // next FramebufferUpdateRequest, sent once the server has answered the one
  // outstanding, is non-incremental. An idle server may not answer for a
  // long time. Sending a request at once is not allowed: with two
  // outstanding, the viewer could no longer tell when none is, as RFB
  // requires before SetPixelFormat. That needs Fence (Continuous Updates).
  //
  // This function does nothing when called before entering the main phase of
  // the protocol (when the framebuffer does not exist yet).
  //
  void refreshFrameBuffer();

  //
  // Timing of sendKeyboardEvent(), sendPointerEvent() and sendCutTextEvent():
  // they are posted to the network thread, and sent in the order they were
  // called, before what the writer sends next; nothing is sent before the
  // protocol is initialized. While the link is busy, pointer moves with
  // the same buttons are coalesced to the latest position
  // (Internal::InputEventQueue).
  //

  //
  // Send a keyboard event. Arguments specify the event as defined in the
  // RFB v.3 protocol specification.
  //
  void sendKeyboardEvent(bool downFlag, UINT32 key);

  //
  // Send a pointer (mouse) event. Arguments specify the event as defined in
  // the RFB v.3 protocol specification.
  //
  void sendPointerEvent(UINT8 buttonMask, const Point &position);

  //
  // Send cut text (clipboard) to the server.
  //
  void sendCutTextEvent(const StringStorage &cutText);

  //
  // Timing of setPreferredEncoding(), allowCopyRect(), setJpegQualityLevel(),
  // setCompressionLevel() and enableCursorShapeUpdates(): they change the
  // set of decoders, which the protocol uses for each rectangle, so each
  // is posted to the network thread, and made there once the protocol is
  // between messages: at once if it is waiting for the next server message,
  // otherwise before it reads that message. The changes are made in order,
  // sending SetEncodings and asking for a refresh (refreshFrameBuffer())
  // where needed; changes made before
  // start() are made once the protocol is initialized, before the first
  // SetEncodings.
  //

  //
  // Set the preferred encoding type. Note that the server is not guaranteed
  // to use this encoding, this is only a recommendation for the server.
  // By default, Tight encoding (EncodingDefs::TIGHT) is the preferred one.
  //
  void setPreferredEncoding(INT32 encodingType);

  //
  // Allow or disallow CopyRect encoding. Correctly designed server is
  // guaranteed not to use CopyRect if disabled via this function (although
  // passing the corresponding message to the server may take time). Normally,
  // CopyRect should be enabled, and it's enabled by default.
  //
  void allowCopyRect(bool allow);

  //
  // Set JPEG image quality level for Tight encoding (in theory, it can apply
  // to other encodings if they use JPEG, but currently, only Tight encoding
  // supports that). Valid levels are in the range 0..9. Also, -1 can be used
  // to disable JPEG and ensure lossless compression (thus, it's possible to
  // say that -1 stands for best image quality).
  //
  void setJpegQualityLevel(int newJpegQualityLevel);

  //
  // Set the compression level for Tight encoging (in theory, it can apply to
  // other encodings as well, and in fact old experimental encoders such as
  // Zlib supported that, but here we do not support those encoders any more).
  // Valid levels are in the range 0..9. Also, -1 can be used to let the
  // server choose actual compression level.
  //
  void setCompressionLevel(int newCompressionLevel);

  //
  // Enable or disable cursor shape updates (enabled by default). If enabled,
  // then server sends information about the cursor shape. If disabled, cursor
  // is shown as a part of usual frame buffer updates.
  //
  void enableCursorShapeUpdates(bool enabled);

  //
  // Ignore or show cursor shape updates (shown by default). If cursor shape
  // updates are enabled but ignored, remote cursor will not be shown. This
  // option probably should not be changed unless something goes really wrong.
  //
  void ignoreCursorShapeUpdates(bool ignored);

  //
  // Work with capabilities is documented in interface CapabilitiesManager.
  // Next methods is implements of CapabilitiesManager.
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

  // Pre-conditions: network thread, or before start() (CapsContainer is
  // not synchronized).
  void getEnabledClientMsgCapabilities(std::vector<UINT32> *codes) const override;
  void getEnabledServerMsgCapabilities(std::vector<UINT32> *codes) const override;
  void getEnabledEncodingCapabilities(std::vector<UINT32> *codes) const override;

  //
  // Copy the frame buffer, as the decoders have left it, into dst.
  //
  // May be called at any time. A test program calls it once the session
  // has ended, because CoreEventsAdapter's update events come from another
  // thread and may not report the last updates.
  //
  // Pre-conditions: dst is not 0.
  // Post-conditions: dst has the dimension and pixel format of the frame
  // buffer and a copy of its pixels, taken under m_fbLock. The copy never
  // contains a cursor, since the notifier draws the cursor and removes it
  // again under the same lock. Before ServerInit the copy is empty.
  // Throws Exception if dst cannot be set up, or std::bad_alloc.
  //
  void copyFrameBuffer(FrameBuffer &dst);

  //
  // Posts work for this object, or for what is registered with it (such as
  // the file transfer), to the network thread, with a reference to the
  // adapter (m_adapterRef), which keeps the adapter's owner, and so this
  // object, alive until the work has run (see start()). Every post for
  // this object goes through here. Does nothing once no reference is left:
  // the owner is being deleted, and the connection has ended. Work posted
  // after the connection has ended still runs, while the owner is alive.
  // Any thread.
  //
  // Pre-conditions: work does not throw.
  //
  void postToLoop(std::function<void()> work);

  //
  // A claim on the adapter's owner, as postToLoop() takes for its work:
  // for work that runs elsewhere and needs this object, and what is
  // registered with it, alive until it ends. Empty once no reference is
  // left (see postToLoop()). Any thread.
  //
  std::shared_ptr<void> claimOwner();

private:
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
  // the frame buffer notifier at once; with gates, it is sent at once.
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
  // the first of them ends; or, with gates, runs run() with runBlocking().
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
  // Sends what m_output queues (m_queuedOutput), whenever it is woken, until
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
  // Read a message type code (UINT8) from the data connection.
  // If the code is 0xFC then it's a beginning of TightVNC-specific extended
  // code so we read next 3 bytes and compose UINT32 message type.
  //
  Task<UINT32> receiveServerMessageType();

  //
  // Read pixel format from the data connection and return it.
  //
  // Pre-conditions: the next bytes of the input are a PIXEL_FORMAT.
  // Post-conditions: the PIXEL_FORMAT has been consumed, and the result is
  // valid (PixelFormat::isValid()). A true-colour format is returned as
  // read. A format without true colour is replaced by the standard format
  // with the same bits per pixel, because its colour fields mean nothing.
  // Throws Exception if the bits per pixel are not 8, 16 or 32, or if a
  // true-colour format is not valid.
  //
  Task<PixelFormat> readPixelFormat();

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
  // at the first zero byte; text longer than MAX_CUT_TEXT_LENGTH is
  // skipped with a warning. Exceptions from the adapter are logged, not
  // passed on. Throws whatever reading the input throws.
  //
  Task<> receiveServerCutText();

  //
  // Receive TightVNC's ServerCutTextUtf8 message (code 0xfc000200) and
  // send its text, decoded as UTF-8, to the adapter.
  //
  // Pre-conditions: the message type has been read.
  // Post-conditions: the message has been read whole. Empty text is
  // ignored; text longer than MAX_CUT_TEXT_LENGTH is skipped with a
  // warning. Invalid UTF-8 becomes U+FFFD, and the text ends at the first
  // zero byte. Exceptions from the adapter are logged, not passed on.
  // Throws whatever reading the input throws.
  //
  Task<> receiveServerCutTextUtf8();

  // Read and discard length bytes from the input.
  Task<> skipBytes(size_t length);

  //
  // Receive SetColourMapEntries server message (code 1) and forget it:
  // for now, color maps are not supported.
  //
  Task<> receiveSetColorMapEntries();

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
  // Post-conditions: m_frameBuffer and m_rectangleFb have the given size
  // and format, filled with black.
  // Throws Exception before changing either frame buffer if the size is
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
  // server format without true colour (see readPixelFormat()).
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
  // or gates (m_input not null).
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

  // With gates, the input gate given to start(), read with runBlocking();
  // otherwise null.
  RfbInputGate *m_input;
  // What the coroutines of the protocol (run() and those it awaits) read;
  // readLoop() feeds it. Used only on the network thread.
  AwaitableReader m_reader;
  // For a socket, m_ownOutput, which queues into m_queuedOutput; with
  // gates, the output gate given to start().
  RfbOutputGate *m_output;
  QueuedOutputStream m_queuedOutput;
  std::unique_ptr<RfbOutputGate> m_ownOutput;

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

  // This is general frame buffer of RemoteViewerCore and local mutex to change him.
  // This frame buffer contain actual state of remote desktop.
  // Cursor painted on him before calling CoreEventsAdapter::onFrameBufferUpdate()
  // and erased after (thread FbUpdateNotifier).
  //
  // Mutex m_fbLock must locked into only this thread, else may be deadlock.
  LocalMutex m_fbLock;
  FrameBuffer m_frameBuffer;

  Dimension m_desktopSize;

  // Decoder work with this framebuffer. It is not actual frame buffer,
  // it only easy buffer, common to all decoders. In future this frame buffer 
  // may be replaced small buffers (e.g. 64KB) into ever decoder.
  //
  // After finish of decoding Decoder copy data to m_frameBuffer.
  // This buffer is not need to blocking: read information is one-thread.
  FrameBuffer m_rectangleFb;

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
