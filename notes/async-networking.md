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
  epoll on Linux, and handles connect with cancellation, timeouts and
  IPv6, which covers the items in `networking.md`. Name resolution on
  Windows is the exception; see "Host name resolution". The alternative
  is our own loop on `WSAPoll`/`poll`: more code to own.

### Host name resolution

Asio's `ip::tcp::resolver` is not good enough on Windows:

- It takes the host as `std::string` and calls the ANSI `getaddrinfo`,
  which reads it in the ANSI code page. A name the GUI holds as UTF-16
  would have to be turned into Punycode with `IdnToAscii` first.
- `async_resolve` runs the blocking call on a hidden thread of Asio's.
  Cancelling only discards the result; the thread stays blocked until
  DNS answers or times out.

So the viewer has its own small resolver interface, with two
implementations:

- **Windows: `GetAddrInfoExW`**, which is asynchronous from Windows 8
  (an `OVERLAPPED` with a completion routine or an event) and can be
  cancelled with `GetAddrInfoExCancel`. It takes the name as UTF-16
  straight from `StringStorage` and converts internationalised names to
  Punycode itself (unless `AI_DISABLE_IDN_ENCODING`).
  - The completion `asio::post`s the result to the connection's
    executor, so the thread it runs on does not matter; wrapped in
    `asio::async_initiate`, the operation takes completion tokens like
    any Asio operation.
  - Each `ADDRINFOEXW` address is copied into an
    `asio::ip::tcp::endpoint`; `asio::async_connect` takes any range of
    endpoints and tries them in turn, which IPv6 needs anyway. The list
    is freed with `FreeAddrInfoExW`.
  - **Lifetime is the hard part.** The `OVERLAPPED`, the result pointer
    and the cancel handle must live until the completion runs, which it
    still does after `GetAddrInfoExCancel` (with `WSA_E_CANCELLED`). So
    the operation's state is on the heap and owned by the completion,
    not by the object that started it.
  - The call also takes a timeout, documented as applying only to
    asynchronous calls; to be confirmed, but it would give a resolve
    timeout for free.
- **Elsewhere: Asio's resolver.** Linux is only a development platform,
  and its strings are UTF-8 already. `getaddrinfo_a` would fit the same
  interface, but gains little: glibc runs it on helper threads too,
  `gai_cancel` cannot cancel a lookup that has started, it notifies by
  signal or on another thread, and before glibc 2.34 it needs `-lanl`.

This replaces `SocketAddressIPv4::resolve`, and covers the cancellable
connect in `networking.md`. `HostPath` already accepts internationalised
names (`string-encoding.md`, "Host names"). About a few hundred lines
with the tests, most of the care going into cancellation.

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
- **TLS** (VeNCrypt with the X509 subtypes; Schannel has no anonymous
  cipher suites, see `other-clients.md`). **The current threading
  conflicts with TLS.** The core thread reads while the GUI thread
  writes, concurrently on one connection. An OpenSSL `SSL` object is
  not safe for concurrent `SSL_read` and `SSL_write`. A Schannel
  context does tolerate one `EncryptMessage` alongside one
  `DecryptMessage` (UltraVNC relies on this), but not alongside the
  handshake, and reads can need to write: renegotiation, and TLS 1.3
  post-handshake messages such as session tickets and key updates. A
  lock around both directions would not work, because a blocked read
  would hold it and starve writes; UltraVNC leaves the handshake path
  unprotected instead. The
  negotiation is also a chain of requests and replies with GUI prompts
  in the middle: VeNCrypt version, subtypes, the TLS handshake partway
  through the RFB handshake, a certificate prompt, then Plain or VNC
  authentication inside the tunnel. As coroutines that is straight-line
  code (`co_await tls.async_handshake(...)`, `co_await
  askUserToTrust(cert)`). Asio provides `ssl::stream` over OpenSSL;
  OpenSSL's memory BIOs also fit the sans-I/O core. The chosen
  implementation is Schannel, which avoids the OpenSSL dependency and
  also fits the sans-I/O core (`other-clients.md`).

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
comes first: the remaining warnings, building as C\+\+20 without
permissive mode, merging the static libraries, ASan and fuzzer builds,
and protocol-level tests. The rewrite itself is decomposed in "Plan for
the rewrite" below, which supersedes this list.

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

## Plan for the rewrite

Agreed 2026-10-21. It comes after the project-level clean-up: the
remaining warnings, C\+\+20 without permissive mode (a prerequisite:
coroutines need C\+\+20), merging the static libraries, ASan and fuzzer
builds, and protocol-level tests.

### Why the rewrite need not be one commit

1. **An awaitable whose `await_ready()` always returns true never
   suspends.** If each read does its blocking read inside the awaitable
   and reports "ready", a coroutine runs exactly like an ordinary call:
   same thread, same order, same exceptions.
2. **So a `syncWait(task)` bridge is safe.** It starts the coroutine and
   checks that it finished; anything else is a logic error. With it, the
   colouring can go bottom-up, one layer per commit: convert the
   decoders, and their caller wraps them in `syncWait(...)`; the next
   commit converts the caller and pushes `syncWait` one level up. Each
   commit compiles and changes nothing.

The read path to colour:

- **19 functions in `RemoteViewerCore`**: `handshake`,
  `negotiateSecurityType`, `readSecurityTypeList`, `initTunnelling`,
  `initAuthentication`, `authenticate`, `readCapabilities`,
  `readCapability`, `clientAndServerInit`, `readPixelFormat`, `execute`,
  `receiveServerMessageType`, `receiveFbUpdate`,
  `receiveFbUpdateRectangle`, `processPseudoEncoding`,
  `receiveSetColorMapEntries`, `receiveServerCutText`,
  `receiveServerCutTextUtf8`, `skipBytes`.
- **The decoders**, through the virtual `DecoderOfRectangle::process` /
  `decode`, plus the pseudo-encodings.
- **`VncAuthenticationHandler`**, through the virtual
  `AuthHandler::authenticate`.
- **`FileTransferReplyBuffer`**. Of the file-transfer reply handlers it
  is the only one that reads; the operations receive the stream but
  never touch it.

No read happens inside a `catch` block, where `co_await` is not
allowed.

### Phase 1: preparation, all synchronous

Each step is useful on its own.

1. **Adapter callbacks only post to the GUI.** `onConnected`, `onBell`
   and `onCutText` stop calling window functions from the core thread.
   This fixes the shutdown deadlock in `concurrency.md`.
2. **The password dialog moves to the GUI thread.** The core posts a
   request and waits on a `std::future`: still blocking, but the wait
   later becomes a `co_await`.
3. **File transfer parses first, then notifies.**
   `FileTransferMessageProcessor` calls the reply parser explicitly,
   then notifies the operations without passing the stream, so the
   coroutine colour stops at the parser instead of spreading through
   every handler interface.
4. **Coroutine infrastructure, unused:** a small lazy `Task<T>` of our
   own, `syncWait`, and a reader awaitable over today's blocking
   stream, with unit tests. Asio stays out of the protocol core
   entirely, so the fuzzers drive it without Asio.
5. **Keepalive.** Independent; this is just a convenient place for it.

The writing side needs no preparation: every write already goes through
`RfbOutputGate` under its lock, so the gate can be re-implemented later
without touching its callers.

### Phase 2: mechanical colouring, each commit a no-op

- Order: decoders → pseudo-encodings and cursor → file-transfer reply
  parser → authentication handler → `RemoteViewerCore`. The last commit
  leaves one `syncWait(run())` in `execute()`.
- Conventions: `[[nodiscard]]` on `Task`; every `Task` is awaited
  immediately and never stored, which keeps borrowed pointers safe;
  `return` becomes `co_return`.
- Checked against the protocol tests and the fuzz corpus: identical
  output.

### Phase 3: really resumable, still on the core thread

- The reader suspends when its buffer runs short. A small driver on the
  existing core thread does a blocking `recv`, appends the bytes and
  resumes the coroutine.
- This is the first change in behaviour, and it does not touch threads.
- **Test:** the same streams delivered one byte at a time, and at
  random split points, must give identical results. That exercises
  every point where the parser can stop. Fuzzers can split their input
  the same way.
- **Audit:** no lock may be held across a `co_await`. It is harmless on
  one thread, but becomes wrong in Phase 4.

### Phase 4: the event loop

The risky part, split up:

1. **Asio from vcpkg**, and a network-thread object
   (`asio::thread_pool(1)`), unused.
2. **Connections move onto the loop.**
   - The driver becomes `async_read_some`; connecting uses Asio's
     connect, and the resolver in "Host name resolution", still IPv4
     only, to keep this to one change.
   - `RfbOutputGate`'s callers stay as they are. Its `flush()` now hands
     the buffered bytes to the loop to write.
   - Listening mode keeps `TcpServer` for now:
     `asio::ip::tcp::socket::assign()` takes over its native `SOCKET`.
   - `RemoteViewerCore` stops being a `Thread`. `stop()` posts a
     cancellation to the loop, and `waitTermination()` waits for the
     connection's coroutine to finish.
3. **The password wait** becomes a `co_await` on a GUI-completed
   operation (`asio::async_initiate`), which resumes on the loop and is
   always completed, with `operation_aborted` on teardown, so no
   coroutine is left suspended forever. Objects passed through
   `LPARAM` are deleted by the receiver, or by the sender if
   `PostMessage` fails.
4. **`FbUpdateNotifier` goes.** The loop copies into the DIB under one
   `std::mutex` and calls `InvalidateRect`.
5. **File transfer moves onto the loop**: operations the GUI starts are
   posted to the loop, local deletion becomes a task on a pool, and the
   file-transfer locks go.
6. **Listening mode moves to an Asio acceptor.**
7. **Deletion**: the `thread` library (the logging and framebuffer locks
   become plain `std::mutex`), the `network` library, `TcpConnection`,
   `WindowsEvent`, `WsaStartup`, and the locks in the gates.

Phase 3 and step 4.2 are where behaviour really changes: Phase 3 lets
the parser stop mid-message, and 4.2 changes which thread owns the
connection. They are kept apart so that a regression points to one or
the other. The largest diff is Phase 2, but it is mechanical.

### Afterwards, as features

- IPv6: resolve with `AF_UNSPEC`, and the bracketed host syntax in
  `networking.md`.
- Timeouts, and pointer coalescing in the post-to-loop path.
- TLS, then Continuous Updates with Fence.
- Independent of all this, on the configuration side: settings in
  `.ini` files only, and saved passwords in Windows Credential Manager
  keyed by `user@host`, separately from the `.ini` files.

### Lifetimes of objects passed to async methods

This section is a note by the author, after working with an agent to clean up the code before the async rewrite.  See also: the note on [“Raw-pointer parameters that could be references”](pointer-parameters.md).

Currently, the codebase passed in `RfbInputGate` and `RfbOutputGate` by reference.

According to [C++ Core Guidelines §CP.53](https://isocpp.github.io/CppCoreGuidelines/CppCoreGuidelines#rcoro-reference-parameters), that's (normally) quite dangerous.  Nevertheless, examples from Asio show asynchronous methods being passed references, e.g. from the [“refactored echo server”](https://think-async.com/Asio/asio-1.38.2/src/examples/cpp20/coroutines/refactored_echo_server.cpp).  The pattern that best works with Asio must be *structured concurrency*:

  - The ‘driver’ of the connection, running on the core thread, owns the connection.
  - Tasks that read from or write to the connection *must not* detach from the driver; the driver owns all tasks under the connection and drives them one at a time.
  - Conceptually, everything outside the driver's call stack needs to somehow hold a *weak reference* to the connection, so that:
    - The GUI thread can await establishment of the RFB connection
    - The GUI thread can send events for the RFB connection
    - The GUI thread can request disconnection (cancellation)
  - When the connection dies:
    - the asynchronous read/write interfaces need to throw an exception, for decoding/encoding tasks to unwind themselves back to the driver
    - further requests from the GUI thread (made through the weak reference) must be ignored

Before the async rewrite, `ViewerInstance` from the GUI side (`tvnviewer`) owns an instance of `RemoteViewerCore`:

  - `RemoteViewerCore` has `Thread` as its base class, already noted earlier.  
  - (This unnatural style of mixing the system-level concept of a `Thread` with the application-level concept of the viewer/connection model looks like a wart imported from Java.)
  - Conceptually, data/operations in `RemoteViewerCore` are ‘shared by default’ between the GUI and the new thread spawned by `Thread`, and then protected by locks.

The cleaner design needs to:

  - Close off all data/operations in the public interface of `RemoteViewerCore` by default.  
  - `RemoteViewerCore` is constructed with connection parameter, possibly with an externally given single-threaded Asio executor.
  - It then proceeds to create the connection on the executor (which runs on a separate thread).
  - It owns all connection state privately.
  - The ‘driver’ of the connection, mentioned above, runs on the executor.
  - `RemoteViewerCore` hands out a proxy for the GUI component (`ViewerInstance`) to invoke operations on.

For the proxy:

  - In principle, there is always concurrency on the writing side:
    - GUI sends event A.
    - Event A needs to write the corresponding RFB frame to the socket.
    - In the middle of writing that RFB frame, the socket cannot send data immediately (i.e. it would block if blocking sockets were used)
    - GUI sends event B in the meantime.
    - Event B must be serialized after the completion of event A.
  - Without an explicit message queue for the writes, we'd need one of the following:
    - an asynchronous lock (binary semaphore) held to write one frame
    - a queue, or ‘channel’, that serializes events from the GUI, and then the ‘driver’ asynchronously waits on the channel, and writes the corresponding frame one at a time to the socket
  - Asio does not have asynchronous locks, but fortunately, it does have channels
    - Channels are labelled as ‘experimental’, but they are pretty much required in our design.  
    - (The use of channels is common in modern asynchronous code in .NET too.)
  - GUI requests to disconnect, which need to jump the queue, can work via Asio ‘cancellation signals’
  - Channels are cleaner than locks in that we can have a single coroutine at the top driving everything for one connection 
    - That coroutine is what we called the ‘driver’ above
    - The channel has to be, in principle, unbounded, as we should not lose messages when the network is slow
  - The ‘weak reference’ mentioned above is only conceptual
    - The implementation need not use an actual `std::weak_ptr` or anything like that
    - `asio::cancellation_signal` and `asio::concurrent_channel` are *already thread-safe* so the GUI thread could just trigger/send on them
    - An already-dead connection would just ignore further cancellations and messages
  - However, `ViewerInstance` needs to wait for the driver inside `RemoteViewerCore`, before destroying shared state such as the `asio::cancellation_signal` or `asio::concurrent_channel` objects
    - `std::shared_ptr` could be used to hold those objects so the coroutine's lifetime can exceed that of `RemoteViewerCore`
    - However, we don't want crashes when shutting down the program, so there needs to be a final synchronous wait for Asio's executor
  
Reference information:

  - [Q&A on the Asio C++ library](cpp-asio.md)
  - [Q&A on C++20 coroutines](cpp-coro.md)
