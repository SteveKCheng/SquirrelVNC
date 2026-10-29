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

#include "util/CommonHeader.h"
#include "util/inttypes.h"

#include "FrameBufferAccess.h"
#include "ViewerBufferSize.h"

#include <asio/any_io_executor.hpp>
#include <asio/ip/tcp.hpp>

#include <memory>

class CapabilitiesManager;
class CoreEventsAdapter;
class FrameBuffer;
class InputStream;
class Logger;
class OutputStream;
class RemoteViewerCoreImpl;
struct PixelFormat;
struct Point;

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
// owns the connection's state. The exception is the CapabilitiesManager
// that getCapabilitiesManager() gives out to the modules extending the
// core, which has its own thread rules.
//
// Each function can be called on any stage of the protocol, unless
// documented otherwise.
//
// Finally, each function of this class may throw exceptions, unless it's
// explicitly stated that it will never do so.
//
//
// The implementation, with the protocol and the network side, is
// RemoteViewerCoreImpl (RemoteViewerCoreImpl.h, for viewer-core only), to
// which each function forwards.
//
class RemoteViewerCore final
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
  // Closes a socket taken over by start() but never used. Never throws.
  //
  // Pre-conditions: if start() was called, the connection has released
  // every reference to the adapter it was given (see start()): the object
  // is destroyed with the adapter's owner, once nothing else holds that.
  //
  ~RemoteViewerCore();

  //
  // What the consumer chooses when it starts a connection (start()).
  //
  struct StartupOptions
  {
    // Whether the server may keep other viewers connected (ClientInit's
    // shared-flag).
    bool sharedFlag = true;

    // The largest frame buffer the server may ask for, in pixels
    // (width * height), in ServerInit or in DesktopSize updates; a larger
    // one ends the session with an Exception, reported through
    // CoreEventsAdapter::onError().
    UINT64 maxFrameBufferPixels = ViewerBufferSize::DEFAULT_MAX_FRAME_BUFFER_PIXELS;

    // Each row of the frame buffer starts a multiple of 2^rowAlignmentLog2
    // bytes from the start of the buffer, as a consumer that paints the
    // pixels may need (a Windows DIB's rows are whole 32-bit words: 2).
    // FrameBuffer::getBytesPerRow() is the row length that results; any
    // bytes past a row's pixels hold nothing in particular. 0: rows are
    // exactly their pixels, with no alignment. At most
    // FrameBuffer::MAX_ROW_ALIGNMENT_LOG2.
    unsigned int rowAlignmentLog2 = 0;
  };

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
  // options are the consumer's choices (StartupOptions). Throws Exception
  // if they are out of range (rowAlignmentLog2 too large), before
  // starting anything.
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
  // with the protocol code, and writing. It also makes every notification
  // through the adapter interface.
  //
  // The decoders draw into the frame buffer on the network thread, without
  // its lock, which only replacing the frame buffer takes there. The
  // adapter is told what changed (onFrameBufferUpdate(), at the end of each
  // FramebufferUpdate and while one waits for the network part way, and
  // onFrameBufferPropChange()); the application reads the pixels from its
  // own thread, through accessFrameBuffer(), which holds the lock. A read
  // may see a rectangle half drawn; its damage, reported once it is done,
  // brings the repaint that shows it whole (see m_fbLock in
  // RemoteViewerCoreImpl.h).
  //
  // Write operations (sending data to the server) are performed on the
  // network thread only, so the output has no lock: the consumer posts
  // events (sendKeyboardEvent() and the like), which the network thread
  // encodes. For a socket, writing only queues the message
  // (QueuedOutputStream), and flushing wakes the writer, which sends it.
  //

  //
  // This version of start() takes the connection as a pair of streams: input
  // is what the server sends, and output receives what the client sends.
  // The input is read with blocking reads on the network thread, which no other connection could share then: it is meant
  // for input in memory, for tests (rfb-replay).
  //
  // Pre-conditions: input and output outlive the connection.
  //
  void start(InputStream *input, OutputStream *output,
             std::shared_ptr<CoreEventsAdapter> adapter,
             const StartupOptions &options);

  //
  // This version initiates new TCP connection to the specified host and port,
  // and then uses the established connection for data exchange. (Asio
  // initializes Windows sockets itself.)
  //
  void start(const TCHAR *host, UINT16 port,
             std::shared_ptr<CoreEventsAdapter> adapter,
             const StartupOptions &options);

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
             const StartupOptions &options);

  //
  // Stop operation: cancels the connection, on the network thread, whatever
  // it is doing (resolving, connecting, reading or writing).
  //
  // This function will return control to the caller immediately, and the
  // connection may run for some time after the function has finished. It
  // releases its references to the adapter once it has ended (see start()).
  //
  // The socket, connected here or taken over, is closed when the connection
  // ends; streams given to start() are not closed.
  //
  void stop();

  //
  // Set the specified pixel format. The viewer will request that pixel format
  // from the server, as well as a full screen update. The pixel format is not
  // guaranteed to be applied immediately. It will be applied when allowed by
  // the protocol, and adapter's onFrameBufferPropChange() will be called after
  // that.
  //
  // Timing: SetPixelFormat is sent ahead of the next FramebufferUpdateRequest
  // (RemoteViewerCoreImpl::updatePixelFormat(), in sendFbUpdateRequest()).
  // - Called before start(), as ViewerWindow does through
  //   setRemoteViewerCore(), it is applied with the first update request.
  //   (ViewerWindow calls it again when it handles the connection; the
  //   format is the same, so nothing more is sent.)
  // - Called later, when the user changes the colour setting, it is posted
  //   to the network thread, and applied with the first update
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
  // The extension point for the modules that extend the core
  // (authentication handlers, the file transfer): they register their
  // capabilities on it before start(), and post work to the network thread
  // through it. See CapabilitiesManager for its thread rules.
  //
  // Post-conditions: the reference is valid as long as this object.
  //
  CapabilitiesManager &getCapabilitiesManager();

  //
  // Copy the frame buffer, as the decoders have left it, into dst.
  //
  // May be called at any time, on any thread but the network thread. A
  // test program calls it once the session has ended.
  //
  // Post-conditions: dst has the dimension and pixel format of the frame
  // buffer and a copy of its pixels, taken under the frame buffer's lock.
  // Before ServerInit the copy is empty.
  // Throws Exception if dst cannot be set up, or std::bad_alloc.
  //
  void copyFrameBuffer(FrameBuffer &dst);

  //
  // Read access to the frame buffer, for painting it: holds the frame
  // buffer's lock until the result is destroyed (see FrameBufferAccess).
  // May be called at any time, on any thread but the network thread.
  //
  // Post-conditions: the result's frame buffer is the connection's, empty
  // (0 x 0) before ServerInit.
  //
  FrameBufferAccess accessFrameBuffer();

private:
  std::unique_ptr<RemoteViewerCoreImpl> m_impl;
};

#endif
