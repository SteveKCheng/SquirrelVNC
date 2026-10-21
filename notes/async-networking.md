# Design: asynchronous networking with C++20 coroutines

A design discussion (2026-10-20) for replacing the blocking,
thread-per-job connection code with one event loop and stackless C++20
coroutines. Nothing here is implemented yet. For the code as it is now,
see `concurrency.md` (threads and locks), `networking.md` (IPv4-only
layers, lookup that cannot be cancelled) and `portability.md` (building
the core on Linux).

## Goals

- **Fewer explicit threads.** Concurrency through tasks and coroutines,
  leaving threads to a lower layer.
- **The GUI thread never blocks on the network.** Today it writes key,
  pointer and clipboard messages directly to the socket.
- **Connect, lookup and authentication can be cancelled, with
  timeouts.**
- **New features that the current threading makes hard:** TLS, and
  Continuous Updates with Fence (see "Features" below).
- **Testability:** fuzzing and replay tests that drive the protocol
  code deterministically, on one thread, from bytes in memory.

## Weaknesses of the blocking sockets today

TCP_NODELAY is already set, so Nagle's algorithm is not among these.

1. **A dead connection is never noticed.** There is no `SO_KEEPALIVE`,
   no timeouts and no application-level heartbeat, so `recv()` blocks
   forever. For a view-only connection this means a frozen screen with
   no error, indefinitely: if the server goes away without a FIN or RST
   (reboot, dropped Wi-Fi, laptop asleep), the viewer waits for an
   update that never comes, and since view-only sends nothing, no send
   fails either.
2. **The GUI thread writes to the socket.** Normally that is one fast
   call per event, but if the server stops reading, the send buffer
   fills and the GUI hangs ("Not Responding") instead of just lagging.
3. **The reading thread also writes**: update requests, and upload data
   in file transfer. While its `send()` blocks, nothing reads. If the
   server is blocked sending at the same time, both sides wait for each
   other.
4. **Pointer motion is not coalesced.** Every mouse move is a 6-byte
   message; on a slow link they queue in the kernel and the remote
   pointer lags behind.
5. **Lookup and connect cannot be cancelled** (`networking.md`).

**Keepalive is worth fixing now**, independently of the rewrite: a few
lines with `SO_KEEPALIVE` plus `SIO_KEEPALIVE_VALS` on Windows
(`TCP_KEEPIDLE` and so on elsewhere).

Not the main cause of slow updates: if frame rate is the problem, look
first at the request/update loop (one round trip of idle time per
frame; fixed by Continuous Updates), decoding on the reading thread, the
three copies per update, and `m_fbLock` held by the notifier while it
copies into the DIB.

### Interim fix considered and rejected

A dedicated **writer thread** per connection, with a queue that the
GUI, the core and file transfer all post to, would fix weaknesses 2–4
with blocking sockets: only the writer calls `send()`, the reader never
blocks on writes, the queue can coalesce pointer moves, and the
`RfbOutputGate` lock goes away. A queue owned by the *core* thread would
not work, because that thread spends its time blocked in `recv()`,
usually in the middle of a rectangle.

Rejected because it adds threads, which is the opposite of the goal,
and because it does not help TLS (below). The event loop below gives
the same benefits.

## Request and reply in RFB

**The handshake is strictly sequential**, about eight round trips,
roughly 300 lines in `RemoteViewerCore` (`handshake`, `authenticate`,
`initTunnelling`, `readCapabilities`, `clientAndServerInit`):

1. Protocol versions are exchanged.
2. The server lists security types and the client picks one. TightVNC's
   own security type adds an exchange of tunnelling and authentication
   capabilities.
3. Authentication. For VNC authentication: a 16-byte challenge, then a
   16-byte DES response. **The password prompt happens in the middle of
   this**, which is why `AuthenticationDialog` runs on the core thread
   today.
4. SecurityResult, with a failure reason string in RFB 3.8.
5. ClientInit (shared flag), then ServerInit: size, pixel format, name,
   and for Tight, the lists of supported message types.

**After the handshake there is almost no request and reply.** Each
direction is an independent stream of messages; no client message waits
for an answer:

- **FramebufferUpdateRequest → FramebufferUpdate** is a trigger, not a
  wait. The server answers whenever it has changes, possibly merged;
  sending the next request after an update is a state change, not a
  blocked call.
- **SetPixelFormat and SetEncodings** take effect "at some point";
  updates already in flight may still use the old format. Fence exists
  to mark the switch point.
- **File transfer** is request and reply in the protocol, but the client
  side is already event-driven: operations are state machines with
  `onFileListReply`, `onDownloadDataReply` and similar handlers, called
  as replies are dispatched. No thread waits for a reply.

### The real difficulty: framing

Server messages carry no length. A FramebufferUpdate's length is known
only by parsing it all the way down: Hextile has a subencoding per 16×16
tile, RRE a subrectangle count, the cursor pseudo-encodings sizes
computed from their headers, and Tight a control byte, an optional
palette and a variable-length size. So there cannot be a central loop
that collects a whole message and hands it to a synchronous parser.

The parsers pull bytes as they need them, through the synchronous
`InputStream`: about 110 read calls, roughly 56 in `RemoteViewerCore`,
40 in the decoders (Tight 11, ZRLE 11, Hextile 8, RRE 7) and 12 in
`FileTransferReplyBuffer`. A non-blocking design must let these parsers
stop in the middle when bytes run out, and resume later.

## How to make the read path resumable

A single event loop that never blocks needs resumable parsing. Without
hand-written state machines, that means some kind of coroutine. Three
ways were considered:

| | Stream interface | Decoders | Testing tools |
|---|---|---|---|
| **Stackless (C++20)** | `read()` becomes an awaitable | every function on the read path becomes a coroutine: about 110 call sites, mechanical | fine: the stack stays ordinary, and a driver can feed bytes from memory |
| **Stackful (fibers)** | unchanged: `read()` switches back to the loop when no bytes are buffered | unchanged | need annotations: ASan must be told about stack switches (`__sanitizer_start_switch_fiber`), Valgrind about each stack (`VALGRIND_STACK_REGISTER`) |
| **Restart on short data** (noVNC) | `read()` raises "need N more bytes" | rewritten so that nothing has side effects until a unit is complete | fine |

- **Stackful coroutines** (Windows Fibers, Boost.Context, Asio's
  `spawn`) would leave every line of protocol and decoder code as it
  is. But each connection needs a stack sized in advance, debuggers show
  them less clearly than threads, and the tools need annotations: the
  sanitizers and Valgrind that motivate the Linux build. Fibers are also
  too unusual for a project like this, when rewriting the read path is
  not too hard.
- **Restarting on short data** needs no coroutines, but is less free
  than it looks. Some state carries over (Hextile's background and
  foreground colours persist from tile to tile), so either whole
  rectangles are re-parsed many times as a large rectangle arrives in
  pieces, or state is saved per tile, which is a small state machine
  after all. Every decoder must also be checked so that nothing stateful
  (zlib in particular) runs before its input is complete; that holds for
  Tight and ZRLE, whose lengths come before their zlib data.

**Decision: stackless C++20 coroutines.** The colouring is real but
mechanical, and limited to the read path. The result is plain C++ that
ASan, Valgrind, libFuzzer and the debugger all handle without help.
MSVC and clang-cl both support coroutines fully, and the tree already
compiles as C++20 (`portability.md`).

## Design sketch

- **Awaitable primitive reads.** `readUInt8()`, `readUInt16()`,
  `readFully()` and so on return awaitables, not coroutines, that
  complete immediately (`await_ready()` is true) when the bytes are
  already buffered. Coroutine frames are then allocated per message or
  rectangle, not per byte.
- **Coroutines for structure.** The handshake, the server-message loop,
  each decoder's rectangle parser and the file-transfer reply parser are
  coroutines. The handshake becomes straight-line code, as it is now.
- **"Sans-I/O" core.** The protocol code takes in bytes and produces
  bytes to send; the transport sits outside it. The parser awaits "more
  bytes", and a driver supplies them from a socket, a pipe, or a fuzz
  buffer. This keeps fuzzing and replay tests single-threaded and
  deterministic.
- **Threads that remain:**
  - the GUI thread, which is unavoidable;
  - **one** network thread running the event loop for all connections,
    replacing both the per-connection core threads and the
    `FbUpdateNotifier` threads;
  - possibly, later, a small pool for heavy decoding (JPEG, large zlib
    inflates), used as tasks.
- **Between the GUI and the loop:**
  - loop → GUI by `PostMessage` and `InvalidateRect` only;
  - GUI → loop by posting to the loop (`asio::post`). Messages to send
    are queued in order; consecutive pointer moves with the same button
    state are coalesced;
  - **prompts are awaits**: the handshake does `co_await` on the
    password (or on "trust this certificate?") and the GUI completes it.
    No dialog runs on a worker thread any more.
- **Library: standalone Asio.** C++20 has coroutines but no scheduler or
  I/O; `std::execution` is C++26 and not in MSVC yet. Asio is
  header-only, in vcpkg, under the Boost Software License (compatible
  with the GPL), supports C++20 coroutines, uses IOCP on Windows and
  epoll on Linux, and handles resolve and connect with cancellation,
  timeouts and IPv6, which covers the items in `networking.md`. The
  alternative is our own loop on `WSAPoll`/`poll`: more code to own.

### Transports, and stdin/stdout

The socket API is not an obstacle to a stdin/stdout tool or to fuzzing,
even today. Only `TcpConnection` knows about sockets;
`RemoteViewerCore::start(RfbInputGate *, RfbOutputGate *, ...)` accepts
any `InputStream`/`OutputStream`. A fuzzer does not use stdin at all:
libFuzzer calls a function with a byte buffer, so it is
`ByteArrayInputStream` → decoder. So stdin/stdout argues for keeping
the transport abstract, not for the rewrite itself.

**Asynchronous stdin is awkward on Windows**: the anonymous pipes a
shell provides do not support overlapped I/O, so IOCP and Asio cannot
read them asynchronously. A test tool there would read synchronously,
or through a helper. Linux has no such problem.

## Features: does the rewrite help?

| Feature | Coroutines specifically | Single event loop |
|---|---|---|
| Desktop resizing | wash | small help |
| H.264 | wash | wash |
| Continuous Updates | small help | real help |
| TLS | clear help | large help, close to required |

Today `rfb/EncodingDefs.h` knows only the plain DesktopSize
pseudo-encoding; ExtendedDesktopSize, Fence, Continuous Updates, H.264
and VeNCrypt are all absent.

- **Desktop resizing.** Server-initiated resizing already works through
  DesktopSize; ExtendedDesktopSize adds screen layouts, and is just
  another pseudo-rectangle to receive. Client-requested resizing is
  SetDesktopSize, answered by an ExtendedDesktopSize rectangle carrying
  a status code: a loose request and reply, where `co_await` would make
  error reporting a little neater. The real work is in the GUI: window
  size, reallocating the DIB, scaling. Today that reallocation runs on
  the notifier thread under nested locks (`setNewFramebuffer`, with its
  own `FIXME`); one loop posting to the GUI simplifies it.
- **H.264** (Open H.264, encoding 50, which neatvnc supports). Each
  rectangle carries a length and flags before its data, so parsing is
  trivial either way. The work is in decoding: state per context, CPU
  cost, ideally hardware (Media Foundation or FFmpeg/openh264, possibly
  D3D11 surfaces), and in-order decoding. Moving it off the reading path
  is a matter of tasks, not coroutines.
- **Continuous Updates** (EnableContinuousUpdates,
  EndOfContinuousUpdates, Fence). Dropping the request/update loop is
  easy either way. Fence is where the threads hurt: a Fence with
  BlockBefore requires everything received earlier to be *processed*
  before the reply goes out, and today processing finishes
  asynchronously on the notifier thread; the reply must also be ordered
  against messages sent from the GUI and core threads. Changing pixel
  format needs a Fence, then SetPixelFormat, then switching decoders
  exactly at the Fence reply. With one loop all of this is natural.
- **TLS** (VeNCrypt, with AnonTLS as a fallback). **The current
  threading conflicts with TLS.** The core thread reads while the GUI
  thread writes, concurrently on one connection; an OpenSSL `SSL` object
  is not safe for concurrent `SSL_read` and `SSL_write`, nor is a
  Schannel context, and reads can need to write (TLS 1.3 key updates and
  other post-handshake messages). A lock around both would not work,
  because a blocked read would hold it and starve writes. The
  negotiation is also a chain of requests and replies with GUI prompts
  in the middle: VeNCrypt version, subtypes, the TLS handshake partway
  through the RFB handshake, a certificate prompt, then Plain or VNC
  authentication inside the tunnel. As coroutines that is straight-line
  code (`co_await tls.async_handshake(...)`, `co_await
  askUserToTrust(cert)`). Asio provides `ssl::stream` over OpenSSL;
  OpenSSL's memory BIOs also fit the sans-I/O core. On Windows the
  alternative is wrapping Schannel, which avoids the OpenSSL dependency.

**TLS is the feature that justifies the rewrite**: doing it before the
event loop means fighting the concurrent reads and writes. Continuous
Updates also gets cleaner after the rewrite; desktop resizing and H.264
can go in either order.

## Related conclusions from the same discussion

- **Merge the static libraries before CMake.** Two targets instead of
  15: **core** (`region`, `io-lib`, `util`, `thread`, `network`, `rfb`,
  `log-writer`, `viewer-core`, `ft-common`, `ft-client-lib`,
  `file-lib`), which must build on Linux and never include
  `<windows.h>`; and **win** (`win-system`, `gui`, `config-lib`,
  `client-config-lib`, plus the Windows-only parts moved out of core:
  `RfbKeySym`, `Keymap`, `ResourceLoader`/`StringTable`, `GetCPUtime`).
  The boundary then becomes enforceable by the Linux build. Unused code
  costs nothing: the linker takes from a static library only the
  objects that resolve a symbol, and `/OPT:REF` (or `--gc-sections`)
  drops unused functions within them; the only trap would be code
  registering itself through static constructors, which this code does
  not do. Keep the directories, so includes, history and comparison with
  TightVNC stay intact. Merge in MSBuild first (cheap, mechanical), so
  CMake copies a settled structure.
- **UTF-8 `TCHAR` on Linux is safe.** Apart from transcoding at the
  protocol boundary, the core never iterates over characters: it only
  searches for and inserts ASCII delimiters (path separators, CR/LF,
  `:`), and in UTF-8 an ASCII byte never occurs inside a multi-byte
  sequence. `toLowerCase()` touches only ASCII under `_istalpha` and is
  used only by Windows command-line parsing. The one count that is not
  a delimiter is the VNC password, cut to 8 units: with UTF-8 that is 8
  bytes, which is what Linux servers (libvncserver, neatvnc) use.
  Character work proper (`RfbKeySym`, the clipboard, edit controls)
  stays in Windows code, in UTF-16.
- **Two encoding bugs** (not security issues), to fix when the
  transcoding is written: legacy cut text is decoded and encoded with
  the system code page (`AnsiStringStorage`, `CP_ACP`) instead of
  ISO-8859-1 (`RemoteViewerCore.cpp`, `receiveServerCutText`), and the
  desktop name likewise (`clientAndServerInit`), although modern servers
  send UTF-8.

## Order of work

**Provisional; the final plan will differ.** Project-level clean-up
comes first: the remaining warnings, building as C++20 without
permissive mode, merging the static libraries, ASan and fuzzer builds,
and protocol-level tests. The decomposition of the rewrite itself is
still being planned.

1. **Keepalive**, on the current code.
2. **Fuzzers and replay tests on the current code.** They work today,
   through `ByteArrayInputStream`. The decoders are the code most
   exposed to attack, and the rewrite rewrites them; a corpus run
   through the old and new decoders, comparing framebuffers, makes a
   ready-made correctness check (differential testing).
3. **Merge the libraries and untangle the layers** (`portability.md`),
   then CMake for the core.
4. **The event loop and coroutine read path**, with Asio.
5. **TLS**, then **Continuous Updates and Fence**. Desktop resizing and
   H.264 in either order.
