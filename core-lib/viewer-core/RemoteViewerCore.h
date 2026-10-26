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
#include "network/socket/SocketIPv4.h"
#include "rfb/FrameBuffer.h"
#include "region/Dimension.h"
#include "region/Point.h"

#include "CapsContainer.h"
#include "CoreEventsAdapter.h"
#include "DecoderStore.h"
#include "io-lib/AwaitableReader.h"
#include "util/Task.h"
#include "FbUpdateNotifier.h"
#include "QueuedOutputStream.h"
#include "ServerMessageListener.h"

#include <asio/any_io_executor.hpp>
#include <asio/awaitable.hpp>
#include <asio/cancellation_signal.hpp>
#include <asio/ip/tcp.hpp>
#include <asio/steady_timer.hpp>

#include <atomic>
#include <condition_variable>
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
// Each function of this class may be called from any thread and from any
// number of threads simultaneously on the same object, unless documented
// otherwise. Also, each function can be called on any stage of the protocol,
// and even from callback functions of the adapter (see more about callbacks
// and the adapter below), unless documented otherwise.
//
// Finally, each function of this class may throw exceptions, unless it's
// explicitly stated that it will never do so.
//
class RemoteViewerCore final : public CapabilitiesManager
{
public:
  //
  // Creates an object that does no protocol work, and starts no thread,
  // until start() is called.
  //
  // To allow logging, pass a pointer to an object which implements the Logger
  // interface (see log-writer/Logger.h). Logging is disabled by default.
  //
  RemoteViewerCore(Logger *logger = nullptr);

  RemoteViewerCore(const RemoteViewerCore &) = delete;
  RemoteViewerCore &operator=(const RemoteViewerCore &) = delete;
  RemoteViewerCore(RemoteViewerCore &&) = delete;
  RemoteViewerCore &operator=(RemoteViewerCore &&) = delete;

  // FIX DOCUMENTATION: What objects will be destroyed? Will a socket object be destroyed
  // which has been passed to start()?
  //
  // The destructor.
  //
  // Before actually destroying the object, it will terminate its active
  // threads and will wait until all the threads are actually finished. So
  // calling the destructor may block the calling thread until all object's
  // internal threads are gone. To prevent this, call stop() and then
  // waitTermination() prior to destroying the object.
  //
  // The destructor never throws an exception. If something goes wrong, it
  // just silently does everything possible to destroy the object cleanly, but
  // is not guaranteed to do so if any object's threads are running at the
  // moment of calling the destructor. That's another reason to call
  // stop() and waitTermination() explicitly prior to destroying the object
  // (unless you are sure there are no active threads).
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
  // (NetworkThread::instance(), which must exist) and return control to the
  // calling thread. Only one of these functions should be called, and it
  // should be called just once.
  //
  // On various events, corresponding callback functions will be called via
  // the CoreEventsAdapter interface (the adapter). To receive these
  // notifications, the application should inherit CoreEventsAdapter and
  // provide a pointer to that subclass to one of the start() functions.
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
  // Write operations (sending data to the server) may be performed from any
  // thread, both within the object and from outside of the object, assuming
  // that proper locking and flushing are used in the corresponding
  // RfbOutputGate object. For a socket, flushing only queues the message
  // (QueuedOutputStream), and the network thread sends it, so no thread
  // waits for the network to send.
  //

  //
  // This version of start() takes any type of connection that can be
  // specified via a pair of abstract "gates" (RfbInputGate and RfbOutputGate).
  // The input is read with blocking reads on the network thread, which no
  // other connection could share then: it is meant for input in memory, for
  // tests (rfb-replay).
  //
  void start(RfbInputGate *input, RfbOutputGate *output,
             CoreEventsAdapter *adapter,
             bool sharedFlag = true);

  //
  // This version initiates new TCP connection to the specified host and port,
  // and then uses the established connection for data exchange.
  //
  // NOTE: RemoteViewerCore will not initialize Windows sockets for you.
  //       If you use this component under Windows, you should call
  //       WindowsSocket::startup() prior to calling function.
  //
  void start(const TCHAR *host, UINT16 port,
             CoreEventsAdapter *adapter,
             bool sharedFlag = true);

  //
  // This version expects a connected TCP socket. For example, it can be used
  // for reverse connections (when servers connect to viewers). It takes the
  // socket over (SocketIPv4::release()): socket is left without one, and
  // the core closes it.
  //
  void start(SocketIPv4 *socket,
             CoreEventsAdapter *adapter,
             bool sharedFlag = true);

  //
  // Set the largest frame buffer the server may ask for, in pixels
  // (width * height). The limit applies to the size in ServerInit and to
  // DesktopSize updates. The default is
  // ViewerBufferSize::DEFAULT_MAX_FRAME_BUFFER_PIXELS.
  //
  // Pre-conditions: start() has not been called.
  // Post-conditions: a larger frame buffer ends the session with an
  // Exception, reported through CoreEventsAdapter::onError().
  // Throws Exception, and changes nothing, if the object was started.
  //
  void setMaxFrameBufferPixels(UINT64 maxPixels);

  //
  // Stop operation: cancels the connection, on the network thread, whatever
  // it is doing (resolving, connecting, reading or writing), and ends the
  // frame buffer notifier's thread.
  //
  // This function will return control to the caller immediately, and the
  // connection may run for some time after the function has finished. To wait
  // for its end after stop(), call waitTermination().
  //
  // The socket, connected here or taken over, is closed when the connection
  // ends; gates given to start() are not closed.
  //
  void stop();

  //
  // Wait until the connection has ended on the network thread, and the frame
  // buffer notifier's thread too. Always call this function prior to
  // destroying this object and connected objects (CoreEventsAdapter), but
  // only if remote viewer core was actually started.
  //
  // This function does not stop the connection. That can be done by calling
  // stop(). If stop() was not and will not be called, and no error occures,
  // this function blocks the calling thread forever.
  //
  // Pre-conditions: not called on the network thread (it would wait for
  // itself), and the network thread is running.
  // Post-conditions: isFinished(), so the object may be destroyed.
  //
  // A GUI need not call it while the connection may still run: the viewer
  // stops the connection when its window is closed, and deletes it once
  // isFinished() (ViewerInstance), never waiting on the GUI thread.
  //
  // NOTE: If remote viewer core isn't started, then function waitTermination()
  // blocks the calling thread forever.
  //
  void waitTermination();

  //
  // Whether nothing of this object is left on the network thread: the
  // connection has ended, finish() has told the adapter, and every piece of
  // work posted for it (postToLoop()) has run; or start() was never called.
  // Then the object may be destroyed without waiting. Any thread; does not
  // wait.
  //
  bool isFinished() const;

  //
  // Set the specified pixel format. The viewer will request that pixel format
  // from the server, as well as a full screen update. The pixel format is not
  // guaranteed to be applied immediately. It will be applied when allowed by
  // the protocol, and adapter's onFrameBufferPropChange() will be called after
  // that.
  //
  // Timing: the format is applied by updatePixelFormat() in the next
  // sendFbUpdateRequest(), which sends SetPixelFormat ahead of that request.
  // - Called before start(), as rfb-replay does and as ViewerWindow does
  //   through setRemoteViewerCore(), or from CoreEventsAdapter::onConnected()
  //   on the protocol thread, it is applied with the first update request:
  //   all of these happen before that request. (ViewerWindow calls it again
  //   when the GUI thread handles the connection, which races as below; the
  //   format is the same, so nothing more is sent.)
  // - Called later from the GUI thread, when the user changes the colour
  //   setting, it races with the protocol thread, so which request carries
  //   it varies from run to run.
  // FIXME: after the asynchronous rewrite (notes/async-networking.md), check
  // that a change from the GUI is posted to the network loop and applied in
  // order with the update requests, so that this timing is deterministic too.
  //
  void setPixelFormat(const PixelFormat &viewerPixelFormat);

  //
  // Pause/resume updating the frame buffer.
  //
  void stopUpdating(bool isStopped);

  //
  // Request full refresh of the framebuffer, so that the whole screen will be
  // re-sent from the server. The refresh is not guaranteed to happen
  // immediately, it will be completed when allowed by the protocol.
  //
  // This function does nothing when called before entering the main phase of
  // the protocol (when the framebuffer does not exist yet).
  //
  void refreshFrameBuffer();

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
  // set of decoders, which only the core thread may touch while it runs, so
  // each only queues its change. The core thread makes the changes in order,
  // sending SetEncodings where needed, before it reads the next server
  // message; changes queued before start() are made once the protocol is
  // initialized, before the first SetEncodings.
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

  void getEnabledClientMsgCapabilities(std::vector<UINT32> *codes) const override;
  void getEnabledServerMsgCapabilities(std::vector<UINT32> *codes) const override;
  void getEnabledEncodingCapabilities(std::vector<UINT32> *codes) const override;

  //
  // Copy the frame buffer, as the decoders have left it, into dst.
  //
  // May be called from any thread, at any time. A test program calls it
  // once the session has ended, because CoreEventsAdapter's update events
  // come from another thread and may not report the last updates.
  //
  // Pre-conditions: dst is not 0.
  // Post-conditions: dst has the dimension and pixel format of the frame
  // buffer and a copy of its pixels, taken under m_fbLock. The copy never
  // contains a cursor, since the notifier draws the cursor and removes it
  // again under the same lock. Before ServerInit the copy is empty.
  // Throws Exception if dst cannot be set up, or std::bad_alloc.
  //
  void copyFrameBuffer(FrameBuffer &dst);

private:
  //
  // Whether start() has been called. Stays true after stop(), or after the
  // connection has ended, as it only records that start() was called.
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
  // Events from the GUI side: each public method that sends a client
  // message, or changes a setting of the session, makes one and passes it
  // to post(), which hands it to the matching handle() function. The
  // handlers do what those methods used to do themselves.
  //
  // This is the representation that the queue to the network thread is to
  // hold (notes/async-networking.md, Phase 4); the public methods stay, so
  // that their callers do not change when it does.
  //
  struct KeyEvent
  {
    bool isDown;
    UINT32 keySym;
  };

  struct PointerEvent
  {
    UINT8 buttonMask;
    Point position;
  };

  struct CutTextEvent
  {
    StringStorage text;
  };

  struct StopUpdatingEvent
  {
    bool isStopped;
  };

  struct RefreshEvent
  {
  };

  struct PixelFormatEvent
  {
    PixelFormat pixelFormat;
  };

  struct PreferredEncodingEvent
  {
    INT32 encodingType;
  };

  struct CopyRectEvent
  {
    bool isAllowed;
  };

  struct JpegQualityEvent
  {
    int level;
  };

  struct CompressionLevelEvent
  {
    int level;
  };

  struct CursorShapeUpdatesEvent
  {
    bool isEnabled;
  };

  struct IgnoreCursorShapeUpdatesEvent
  {
    bool isIgnored;
  };

  using ClientEvent = std::variant<KeyEvent,
                                   PointerEvent,
                                   CutTextEvent,
                                   StopUpdatingEvent,
                                   RefreshEvent,
                                   PixelFormatEvent,
                                   PreferredEncodingEvent,
                                   CopyRectEvent,
                                   JpegQualityEvent,
                                   CompressionLevelEvent,
                                   CursorShapeUpdatesEvent,
                                   IgnoreCursorShapeUpdatesEvent>;

  //
  // Handles event with its handle() function: at once, on the calling
  // thread; except for the events that change m_decoderStore
  // (changesDecoderStore()), which it queues for applyPendingEvents(), on
  // the core thread. Any thread. Throws what the handler throws, when it is
  // called here.
  //
  void post(ClientEvent event);

  //
  // Whether event is one that changes m_decoderStore.
  //
  static bool changesDecoderStore(const ClientEvent &event);

  //
  // Handles the events queued by post(), in the order they were posted.
  // Called on the core thread only, where no decoder is in use: before each
  // server message is read, and once after the protocol initialization.
  //
  // Post-conditions: the events queued when it was called have been
  // handled, unless a handler threw; the events after that one are dropped.
  // Throws what the handlers throw.
  //
  void applyPendingEvents();

  //
  // The handlers of post(), one for each public method: see that method for
  // what each does, and what it may throw.
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
  // address that accepts; or takes over m_acceptedSocket. Then sets the
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
  // IOException, so that the protocol code ends too.
  //
  asio::awaitable<void> writeLoop();

  //
  // Reports how the connection ended to the adapter, as onAuthError(),
  // onDisconnect() or onError(), and marks the core finished for
  // waitTermination(). The completion of drive(), on the network thread.
  //
  void finish(std::exception_ptr failure);

  //
  // Posts work for this object to the network thread, unless the connection
  // has finished (finish()), counting it until it has run, so that
  // isFinished() and waitTermination() know when nothing is left that
  // refers to this object. Every post for this object goes through here.
  // Any thread.
  //
  void postToLoop(std::function<void()> work);

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
  void start(CoreEventsAdapter *adapter, bool sharedFlag);

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

  // What start() was given: a host and port to connect to; a socket taken
  // over (INVALID_SOCKET if none); or gates (m_input not null).
  StringStorage m_host;
  UINT16 m_port;
  SOCKET m_acceptedSocket;

  // The network thread's executor, for the connection.
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
  // Set by finish(); and the number of postToLoop() works not yet run. For
  // isFinished() and waitTermination(); guarded by m_finishLock, and
  // m_finishChanged is notified when either changes.
  mutable std::mutex m_finishLock;
  std::condition_variable m_finishChanged;
  bool m_isFinished;
  int m_pendingPosts;

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

  CoreEventsAdapter *m_adapter;

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

  // This flag is set after call start().
  mutable LocalMutex m_startLock;

  // The events queued by post() for applyPendingEvents(). Guarded by
  // m_pendingEventsLock.
  std::vector<ClientEvent> m_pendingEvents;
  std::mutex m_pendingEventsLock;
  bool m_wasStarted;

  // The frame buffer limit, in pixels. Written only before start(), under
  // m_startLock, so the threads read it without a lock.
  UINT64 m_maxFrameBufferPixels;

  // This flag is set after onConnected().
  mutable LocalMutex m_connectLock;
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

  LocalMutex m_pixelFormatLock;
  bool m_isNewPixelFormat;
  PixelFormat m_viewerPixelFormat;
  // True once SetPixelFormat has been sent. Protected by m_fbLock.
  bool m_isPixelFormatSent;

  LocalMutex m_refreshingLock;
  bool m_isRefreshing;

  LocalMutex m_freezeLock;
  bool m_isFreeze;

  LocalMutex m_requestUpdateLock;
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
