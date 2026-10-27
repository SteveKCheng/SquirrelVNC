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
- **The GUI thread never blocks on the network.** ~~Today it writes key,
  pointer and clipboard messages directly to the socket.~~ *Done for
  input in Phase 4 step 2e: those messages are posted to the network
  thread; file-transfer messages remain (step 5).*
- **Connect, lookup and authentication can be cancelled, with
  timeouts.**
- **New features that the current threading makes hard:** TLS, and
  Continuous Updates with Fence (see "Features" below).
- **Testability:** fuzzing and replay tests that drive the protocol
  code deterministically, on one thread, from bytes in memory.

## Weaknesses of the blocking sockets today

TCP_NODELAY is already set, so Nagle's algorithm is not among these.
*(Only on outgoing connections, it turned out; accepted ones, in
listening mode, also have it since 2026-10-26.)*

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
lines with `SO_KEEPALIVE` plus ~~`SIO_KEEPALIVE_VALS` on Windows
(`TCP_KEEPIDLE` and so on elsewhere)~~ *`TCP_KEEPIDLE`, `TCP_KEEPINTVL`
and `TCP_KEEPCNT`, which Windows has too since Windows 10 version 1709,
with the same names as Linux; `SIO_KEEPALIVE_VALS` cannot set the probe
count.* Done in Phase 1 step 5 (below).

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
   this**, which is why `AuthenticationDialog` ran on the core thread
   (until Phase 1 step 2, below).
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
  - **No lock across `GetAddrInfoExCancel`** (found 2026-10-27, in
    step 7h): it waits for a completion routine that is running. The
    first version held a mutex across it, which the completion routine
    also took, and deadlocked when the two met (about one run in 30 of
    the cancellation test). The API cannot clear the cancel handle at
    the moment Windows stops accepting it, so a narrow race is accepted
    instead: a cancel just after the completion has run is given a
    stale handle, and fails with `WSA_INVALID_HANDLE`. .NET accepts the
    same race (`NameResolutionPal.Windows.cs` in dotnet/runtime).
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
  never touch it. (Since Phase 1 step 3, only its `readReply()` takes
  the stream.)

No read happens inside a `catch` block, where `co_await` is not
allowed.

### Phase 1: preparation, all synchronous

Each step is useful on its own.

1. **Adapter callbacks only post to the GUI.** `onConnected`, `onBell`
   and `onCutText` stop calling window functions from the core thread.
   ~~This fixes the shutdown deadlock in `concurrency.md`.~~ *That
   deadlock turned out not to be reachable (`concurrency.md`, "Hazards");
   the step still removes blocking cross-thread window calls and data
   races, and the network thread of Phase 4 must never block on the GUI.*
   **Done** (2026-10-25): `onConnected` keeps only the file-transfer
   set-up on the core thread and posts `WM_USER_CONNECTED`; `onBell`
   posts `WM_USER_BELL`; `onCutText` stores the latest text under a mutex
   and posts `WM_USER_CUT_TEXT`. The contract is documented on
   `CoreEventsAdapter`.
2. **The password dialog moves to the GUI thread.** The core posts a
   request and waits on a `std::future`: still blocking, but the wait
   later becomes a `co_await`. **Done** (2026-10-25):
   `ViewerWindow::askPassword()` and `cancelPasswordRequest()`; see
   `concurrency.md`, "Threads". The cancellation is the synchronous
   version of Phase 4 step 3's `operation_aborted` on teardown.
3. **File transfer parses first, then notifies.**
   `FileTransferMessageProcessor` calls the reply parser explicitly,
   then notifies the operations without passing the stream, so the
   coroutine colour stops at the parser instead of spreading through
   every handler interface. **Done** (2026-10-25):
   `FileTransferReplyBuffer` is no longer a listener; its handlers became
   private `read*Reply()` functions behind `readReply()`, which
   `processRfbMessage()` calls before locking the listener list and
   notifying. `FileTransferEventHandler`'s methods take no stream.
4. **Coroutine infrastructure, unused:** a small lazy `Task<T>` of our
   own, `syncWait`, and a reader awaitable over today's blocking
   stream, with unit tests. Asio stays out of the protocol core
   entirely, so the fuzzers drive it without Asio. **Done** (2026-10-25):
   `core-lib/util/Task.h` (`Task<T>`, `syncWait`) and
   `core-lib/io-lib/AwaitableReader.h`, tested by
   `harnesses/unit-test/TaskTests.cpp`. Choices, agreed with Steve:
   - **Lazy**, not eager like .NET: no atomics, since nothing can complete
     on another thread before it is awaited; destroying an unstarted
     Task is harmless; and only lazy frames can be elided (the child's
     lifetime provably nests in the parent's). The cost is that the
     arguments must stay alive until the Task is awaited, so
     `operator co_await` takes only an rvalue: `co_await f(...)`, or a
     visible `co_await std::move(task)`.
   - **Symmetric transfer.** A loop of a million awaits that complete
     without suspending, and a chain of 100,000 nested Tasks, run in
     bounded stack in Debug and Release, Win32 and x64: MSVC makes the
     transfer a tail call even without optimisation. As a check of the
     test, a variant whose final awaiter calls `resume()` instead
     overflows the stack in Debug x64.
   - Exceptions are stored and rethrown at the `co_await`. Destroying a
     started but unfinished Task is asserted against, as unwinding is
     meant to be cooperative. `syncWait` throws `std::logic_error` if the
     Task suspends, leaking its frame rather than destroying a
     half-finished chain.
   - Frames come from the global `operator new`; a pool can come later
     (`cpp-coro.md`).
   - `AwaitableReader`'s awaitables are always ready, and read, blocking,
     in `await_resume`, so exceptions come from the `co_await` as from
     the calls they replace.
5. **Keepalive.** Independent; this is just a convenient place for it.
   **Done** (2026-10-26): `SocketIPv4::enableKeepAlive()`, called by
   `TcpConnection::connect()` for every socket, outgoing or accepted:
   30 seconds idle, then a probe every 5 seconds, reset after 10
   unanswered, so a dead server is noticed after about 80 seconds. A
   failure to set the options is logged, and the connection goes on
   without them. (Doing this showed that accepted connections had no
   TCP_NODELAY; fixed separately.)
6. **A narrow interface for `RemoteViewerCore`** (added 2026-10-25; see
   "Corrections and decisions" at the end). The GUI side
   (`ViewerInstance`, `ViewerWindow` and the dialogs) talks to the
   connection only through a small handle: send a typed input event,
   request disconnection, and receive notifications. Other shared state
   is closed off, except the framebuffer. Still synchronous and
   implemented over today's threads, so Phase 4 replaces what is behind
   the handle without touching the GUI. How far this goes is decided
   when we get there.
   **Done** (2026-10-26), as far as was reasonable:
   - Unused public parts removed: the constructors that started the core
     themselves, and `getDesktops()`. `wasStarted()`,
     `allowUtf8Clipboard()` and `getProtocolString()` are private.
   - Steve's decision: the public methods stay as the interface
     (`sendKeyboardEvent()`, `setPixelFormat()` and so on), and the event
     representation is private, so that callers and text searches do not
     depend on it. Each method makes a small event struct and passes it to
     the private `post()`, which dispatches a private `std::variant`,
     `ClientEvent`, to one `handle()` overload per event. In Phase 4,
     `post()` queues the events for the network thread.
   - Already now, `post()` queues the five events that change the decoder
     set (preferred encoding, CopyRect, JPEG quality, compression level,
     cursor shape updates), and the core thread handles them between
     server messages: `DecoderStore` has no lock, and the GUI thread used
     to change its map while the core thread looked up decoders in it.
     The other events are still handled on the calling thread.
   - `CoreEventsAdapter::onConnected()` receives a `ConnectionInfo`
     (desktop name, protocol string, enabled message capabilities), which
     `ViewerWindow` keeps on the GUI thread; `getRemoteDesktopName()` is
     gone.
   - Left for Phase 4, as planned: file-transfer requests still go to the
     output gate directly, from the GUI and core threads (step 5), and
     the frame-buffer callbacks still come from the notifier thread
     (step 4).

~~The writing side needs no preparation: every write already goes through
`RfbOutputGate` under its lock, so the gate can be re-implemented later
without touching its callers.~~ *Superseded: writes from the GUI become
typed events through the handle of step 6, encoded on the network
thread.*

### Phase 2: mechanical colouring, each commit a no-op

- Order: decoders → pseudo-encodings and cursor → file-transfer reply
  parser → authentication handler → `RemoteViewerCore`. The last commit
  leaves one `syncWait(run())` in `execute()`.
- Conventions: `[[nodiscard]]` on `Task`; every `Task` is awaited
  immediately and never stored, which keeps borrowed pointers safe;
  `return` becomes `co_return`.
- Checked against the protocol tests and the fuzz corpus: identical
  output.

**Done** (2026-10-26), in five commits, each building in Debug Win32,
Debug x64, ReleaseAsan x64 and ReleaseFuzz x64 with the replay cases and
the fuzz corpus passing:

1. The rectangle decoders (`DecoderOfRectangle::process()`/`decode()`
   and Tight's and ZRLE's helpers that read the network). ZRLE's tile
   readers stay synchronous: they read the inflated data from memory.
2. `processPseudoEncoding()` (the rich cursor).
3. File transfer: `FileTransferReplyBuffer::readReply()` and the six
   `read*Reply()` functions that read, `readCompressedDataBlock()`,
   `processRfbMessage()` and `ServerMessageListener::onServerMessage()`.
   The replies that are only a message type lost their unused stream
   parameter; the file list is still parsed synchronously from memory.
4. `AuthHandler::authenticate()` and `VncAuthentication::vncAuthenticate()`.
   Writes stay synchronous.
5. `RemoteViewerCore`: the 19 functions that read, or call those that
   do, read through a member `std::optional<AwaitableReader> m_reader`;
   `execute()` keeps its `catch` handlers around `syncWait(run())`.

Remarks for Phase 3:

- `rfb-fuzz`'s `FuzzSession` still reads messages synchronously and runs
  the decoders and pseudo-encodings with `syncWait()`; it has to become a
  driver too before fuzzing with split input.
- No `co_await` is in a scope that holds a lock: checked over all 49
  coroutines with `tools/coroutine-colouring/check-locks.py`, which is to
  be run again in the Phase 3 audit.
- Decoder and pseudo-encoding coroutines take a borrowed
  `AwaitableReader &`, and frame-buffer pointers and references; safe
  because each `Task` is awaited at once.

**Scripts** (`tools/coroutine-colouring/`): `colour.py` made layer 1, and
`colour-core.py` with `split-execute.py` layer 5; `realign.py` re-aligns
continuation lines after a changed `(`, and `dropmarked.py` removes the
lines marked for deletion (done after realigning, which needs the line
count unchanged). Layers 2 to 4 were small enough to edit with one-off
scripts, not kept. Each result was reviewed by hand.

### Phase 3: really resumable, still on the core thread

How the reader suspends the protocol code and the driver resumes it is
explained in `async-networking-resumption.md`.

- The reader suspends when its buffer runs short. A small driver on the
  existing core thread does a blocking `recv`, appends the bytes and
  resumes the coroutine.
- The core's other waits (the password, later the certificate prompt
  and the start of TLS) become requests to the driver in the same way;
  see "The boundary between the core and Asio" below. In this phase the
  driver serves the password request with the blocking wait of Phase 1
  step 2.
- This is the first change in behaviour, and it does not touch threads.
- **Test:** the same streams delivered one byte at a time, and at
  random split points, must give identical results. That exercises
  every point where the parser can stop. Fuzzers can split their input
  the same way.
- **Audit:** no lock may be held across a `co_await`. It is harmless on
  one thread, but becomes wrong in Phase 4.

**Done** (2026-10-26):

- `AwaitableReader` holds the bytes received, the pending read, the
  waiter, a request and a sticky error; `runBlocking()` is the blocking
  driver; `Task` has `start()`, `isDone()` and `result()`. Unit tests in
  `TaskTests.cpp`.
- `RemoteViewerCore::execute()` connects, then runs `run()` with
  `runBlocking()` over `m_input`. Connecting is the driver's part, as it
  will be in Phase 4.
- The VNC password is a request (`VncAuthenticationHandler`'s
  `PasswordRequest`), served by the driver with the blocking
  `getPassword()`.
- Tests: `run-tests.ps1` replays every case whole, one byte at a time,
  and in random pieces (`rfb-replay --split`, `--seed`); all pass.
  `rfb-fuzz` reads every input whole and in random pieces and aborts if
  the results differ (checked against a planted bug).
- Audit: `check-locks.py` finds no `co_await` under a lock in the 57
  coroutines.
- Found while fuzzing, not caused by the change: `ZrleDecoder::readAndInflate()`
  allocated the compressed length the server sends (up to 4 GiB) before
  reading any of it. Fixed with a bound from the rectangle's size
  (`[SECURITY]` commit; `notes/security.md`).
- Regression found in the GUI (Steve, 2026-10-26): updates were missed.
  The driver asks its input for up to 64 KiB, and
  `BufferedInputStream::read()`, given more than it held, copied what it
  had and then blocked in `recv()` for the rest, holding bytes that the
  protocol code needed before it would send the next update request: a
  deadlock until the server sent something of its own accord. It now
  returns what it holds. The replay tests could not catch it: their
  input never blocks. A unit test now counts the reads of a
  `BufferedInputStream`'s source.

### Phase 4: the event loop

The risky part, split up:

1. **Asio from vcpkg**, and a network-thread object
   (`asio::thread_pool(1)`), unused.
2. **Connections move onto the loop.**
   - The driver becomes `async_read_some`; connecting uses Asio's
     connect, and the resolver in "Host name resolution", still IPv4
     only, to keep this to one change.
   - ~~`RfbOutputGate`'s callers stay as they are. Its `flush()` now hands
     the buffered bytes to the loop to write.~~ *Superseded:* the handle
     from Phase 1 step 6 posts typed events to the loop, which encodes
     them into the connection's one output buffer, alongside the core's
     own messages; a single writer drains it.
   - ~~Listening mode keeps `TcpServer` for now:
     `asio::ip::tcp::socket::assign()` takes over its native `SOCKET`.~~
     *Until step 6, which replaced `TcpServer` with an Asio acceptor.*
   - `RemoteViewerCore` stops being a `Thread`. `stop()` posts a
     cancellation to the loop~~, and `waitTermination()` waits for the
     connection's coroutine to finish~~. *Superseded:* the viewer
     instance is shared by the GUI side and the driver
     (`std::shared_ptr`, "Ownership by `std::shared_ptr`" below), so the
     GUI side does not wait for one connection; the only synchronous wait
     is joining the network thread at process exit.
3. **The password wait** becomes a `co_await` ~~on a GUI-completed
   operation (`asio::async_initiate`)~~ *in the driver, on a
   GUI-completed operation (`asio::async_initiate`), serving the core's
   password request (see "The boundary between the core and Asio"); the
   core itself only suspends on the request*, which resumes on the loop
   and is always completed, with `operation_aborted` on teardown, so no
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

**Revised order of step 2 onwards** (agreed with Steve, 2026-10-26; the
numbered list above stays as the original plan). Step 1 is done:
`NetworkThread`, an `asio::thread_pool` of one thread, with Asio 1.32
from vcpkg.

- **2a. Resolver**, unused: `GetAddrInfoExW`, asynchronous and
  cancellable, wrapped with `asio::async_initiate`, IPv4 only for now
  ("Host name resolution").
- **2b. The loop takes over the connection.** The driver is an
  `asio::awaitable` on the network thread: resolve, connect (or
  `assign()` the socket accepted in listening mode), read with
  `async_read_some` into the reader. Writes stay as they are:
  `RfbOutputGate` sits over a stream that queues each flushed message
  and wakes the loop, whose one writer drains the queue with
  `async_write`. That keeps the order of flushes from all threads, and
  batches messages queued together into one send. Writing can stay
  synchronous for the protocol code because each message is small and
  complete in memory before it is queued. `RemoteViewerCore` stops being
  a `Thread`; `waitTermination()` still waits for the driver. The
  password is still served by the blocking `getPassword()`, now on the
  network thread, stalling other connections while its dialog is open:
  the shortest possible interim.
  **Done** (2026-10-26). `RemoteViewerCore` is no longer a `Thread`:
  `start()` spawns `drive()` on `NetworkThread::instance()` (owned by
  `TvnViewer`, declared before its viewer instances so that it is joined
  after them; Asio keeps its own WinSock reference, so the order against
  `WindowsSocket::cleanup()` does not matter). `readLoop()` and
  `writeLoop()` run in a parallel group with `wait_for_one()`; a
  `QueuedOutputStream` under `RfbOutputGate` queues each flushed message
  and posts a wake, while holding its lock, so no wake is lost and none
  comes after the queue is closed. When the protocol ends by itself, what
  it queued last is still sent. `stop()` posts the cancellation, which
  also cancels a lookup or connect in progress. `waitTermination()` waits
  for the end of the driver, then for a round trip through the loop, so
  nothing posted for the core is left. `TcpConnection` is gone; the
  socket options (TCP_NODELAY, keepalive) are set on the Asio socket.
  `rfb-replay --tcp` runs every case through a real socket
  (`testing.md`). Not yet tried in the GUI: listening mode, which takes
  over the accepted socket (`SocketIPv4::release()`); deferred by Steve.
  *Step 6 replaced that path.*
- **2c. Ownership.** The viewer instance is shared by the GUI side and
  the driver (`std::shared_ptr`; see "Ownership by `std::shared_ptr`"
  below), so closing a window no longer waits for its connection.
  `NetworkThread` is ~~a singleton in lifetime:~~ one instance,
  made at the start of `TvnViewer` and joined in its destructor, after
  the connections are stopped and before WinSock is shut down~~, with a
  static accessor valid only during that lifetime (test programs make
  their own)~~. Not a function-local static, whose destruction would come
  after `main()`, after WinSock cleanup, in no defined order. *The
  static accessor `NetworkThread::instance()` is gone (Steve,
  2026-10-26): `TvnViewer` owns the thread and gives its executor to
  each `ViewerInstance`, which gives it to the constructor of
  `RemoteViewerCore`. No action at a distance, and a test may run as
  many loops as it likes. The execution context must outlive the core
  and its posted work, a documented precondition, as before.*
  *First done (37a728a) with deletion once the connection had finished,
  polled by `ViewerCollector`, and no shared ownership; redone with
  `std::shared_ptr` as described below (Steve, 2026-10-26).*
  **Done** (2026-10-26):
  - `RemoteViewerCore::start()` takes a `std::shared_ptr<CoreEventsAdapter>`
    and keeps only a `std::weak_ptr`. The completion handler of
    `drive()` holds the reference and releases it from a handler of its
    own, posted after `finish()`, once Asio has finished with the
    coroutine. `postToLoop()` takes one for each piece of work.
  - `isFinished()`, `waitTermination()` and the counting of posts are
    gone; the destructor asserts that no reference is left.
  - `ViewerInstance` is `enable_shared_from_this`. `start()` gives the
    core an aliasing pointer to its window, then gives the window its own
    reference (`ViewerWindow::setOwner()`), released at `WM_NCDESTROY`.
  - `WindowsApplication::postToMessageLoop()` queues work that
    `processMessages()` runs before dispatching each message, so never
    inside a nested loop. `ViewerCollector::addInstance()` gives the
    instance a deleter that posts its deletion there. The
    `m_isInPasswordDialog` exception, `WM_USER_STOP` (which sent
    `WM_DESTROY` without destroying the window) and the timer's
    deletions are gone; the timer only checks for the end of the
    application.
  - `ViewerCollector::destroyAllInstances()` destroys the windows still
    open and runs the deletions itself (`waitForPostedWork()`,
    `runPostedWork()`), as the message loop has ended by then.
  - `rfb-replay` gives the core a reference that owns nothing and
    fulfils a promise when it is released, in place of
    `waitTermination()`.
- **2d. The password** as a request served without blocking: posted to
  the GUI, answered by resuming the driver on the loop, and always
  completed, with `operation_aborted` on teardown (step 3 above). It
  comes before typed events because it pins down the GUI ↔ network
  thread interaction that the rest builds on. **Done** (2026-10-26):
  `AwaitableReader::Request` has `start(done)` and `cancel()` instead
  of a blocking `serve()`; the driver awaits it with an Asio operation
  (`RemoteViewerCore::serveRequest()`), whose completion is posted
  through `postToLoop()` and whose cancellation slot calls `cancel()`. The
  password: `VncAuthenticationHandler::requestPassword(reply)` and
  `cancelPasswordRequest()`; `ViewerWindow::requestPassword(reply)` posts
  to the GUI, and the dialog's answer, or a cancellation, calls `reply`
  exactly once. `runBlocking()` still waits for a request, on its own
  thread. `rfb-replay`'s `ReplayAuthHandler` replies from a thread of
  its own, so the tests cover a reply from another thread.
- **2e. Typed events**: `post()` becomes `asio::post`, encoding on the
  loop, with pointer moves coalesced; deferred until the byte queue of
  2b has been proven, to keep each step's risk small. **Done**
  (2026-10-26):
  - After `start()`, `post()` posts each event to the network thread
    (`postToLoop()`); before it, the event is handled at once, as nothing
    runs yet.
  - The five events that change the decoder set still wait for a message
    boundary (`applyPendingEvents()`). A handler on the loop runs
    whenever the protocol is suspended, often in the middle of a
    rectangle that holds a decoder the event could remove. Only the lock
    of that queue goes: it is used on the network thread alone. ~~Applying
    them at once when the protocol is idle at a boundary is deferred
    (Steve: minor).~~ *Done after Steve met it (an idle server never sent
    the message that would apply them): while `run()` waits for the type
    of the next message (`m_isAtMessageBoundary`), they are applied at
    once, sending SetEncodings.*
  - *Refresh on an idle server still waits* (2026-10-26).
    `refreshFrameBuffer()` makes the next FramebufferUpdateRequest
    non-incremental, and that request is sent only once the server has
    answered the one outstanding, which an idle server may not do for a
    long time. Sending a full request at once was tried and reverted
    before committing: RFB forbids SetPixelFormat with a request
    outstanding ("impossible to determine if the next FramebufferUpdate
    is using the new or the previous pixel format"), and one update may
    answer several requests, so with two outstanding the viewer could
    never again tell when none is. Doing it right needs Fence, with
    Continuous Updates.
  - Key, pointer and cut-text events go to `Internal::InputEventQueue`,
    which `writeLoop()` encodes before each send. While a send is in
    progress, a pointer move replaces the move just before it; a press
    or release (a change of button mask) is never merged, nor is a move
    across a key or cut text. The pointer position goes to the frame
    buffer notifier at once.
  - The event types live in `viewer-core/ClientEvents.h`, in namespace
    `Internal`: by convention for `RemoteViewerCore` and its unit tests
    only (Steve), so that the coalescing rule is tested directly
    (`ClientEventsTests.cpp`).
- ~~Then steps 4 to 7 as above.~~ *Then steps 5 to 7; step 4 is
  deferred (Steve, 2026-10-26).* Profiling (`cpu-usage.md`) shows that
  removing `FbUpdateNotifier` costs nothing once the decoders write
  straight into the DIB, but that the per-pixel loops and the JPEG
  conversion should be fixed first, and that step 4 as written (the
  loop copies into the DIB) would only move the notifier's copy onto
  the network thread. It becomes "decode into the DIB", after the fixes
  in `cpu-usage.md`, "What to do". Until then step 7 keeps
  `WindowsEvent`, which the notifier uses.
- **Step 5, file transfer on the loop** (agreed with Steve, 2026-10-26).
  Today the dialog calls `FileTransferCore` on the GUI thread (starting
  operations, so their first request is sent from there), the reply
  handlers call the dialog on the network thread (`setProgress()` and the
  log by cross-thread `SendMessage`; "target file exists" shows its modal
  dialog *on the network thread*, stalling every connection), the local
  delete runs on a `Thread` nobody waits for, and `m_isTerminating` and
  `m_state` are shared without synchronization. A connection that drops
  leaves the dialog busy for ever. The design: all file-transfer state
  belongs to the network thread; the dialog posts commands to it and gets
  posted events back.
  - **5a. Events posted to the dialog.** `FileTransferEvents`, the
    pattern of `ViewerCoreEvents`: a member of `ViewerWindow`, given to
    the file transfer once as its `FileTransferInterface` (no more
    swapping it on the GUI thread while the network thread calls it). It
    keeps progress (the latest only) and log messages under its lock and
    posts `WM_FT_*` to the dialog, which attaches at `WM_INITDIALOG` and
    detaches at `WM_DESTROY`. `PostMessage` to the dialog rather than
    `postToMessageLoop()` (Steve): a posted message is also dispatched
    inside modal loops (the "file exists" dialog, message boxes), so
    progress keeps showing, whereas work at the top of the loop would
    wait for them to close. Interim: the calls the core makes on the GUI
    thread are passed straight on, and "target file exists" still runs
    on the network thread. **Done** (2026-10-27).
  - **5b. Commands posted to the loop**, through a public
    `RemoteViewerCore::postToLoop()` holding a claim on the instance, so
    `FileTransferCore` is touched on one thread only; the dialog keeps its
    own "busy" state and supported operations. When the connection ends,
    the current operation is aborted and the dialog told. **Done**
    (2026-10-27):
    - `FileTransferCapability::post(command)` runs a command with the
      `FileTransferCore` on the network thread. The dialog sets itself
      busy and disables its controls when it posts, since the start is
      no longer synchronous, and a second operation started meanwhile
      would replace the first while its reply is in flight.
    - What the core did for the GUI thread moved to the dialog: the
      supported operations (from the `ConnectionInfo`, by `ViewerWindow`),
      "busy", listing local folders, and what to refresh after each
      operation (`onUpdateState()`). The remote file list comes with the
      end of the file-list operation (`onFtFileList()`).
    - An operation that cannot start, because the connection is closed,
      reports the error and its end, instead of a message box
      (`raise()`, which would now be on the network thread).
    - When the connection ends (the adapter's `onDisconnect()`,
      `onAuthError()`, `onError()`), an operation on the server is told
      to stop, and the dialog gets `WM_FT_CONNECTION_ENDED`: no more
      replies will come, so it stops being busy and disables its
      controls. A local delete goes on.
    - `RemoteViewerCore::postToLoop()` is public; work posted after the
      connection has ended still runs while the owner is alive.
  - **5c. "Target file exists" as a request**: the operation records
    where it stopped, the question is posted to the dialog, and the
    answer is posted back to the loop (`onTargetFileExistsAnswer()`), a
    callback continuation rather than rewriting the operations as
    coroutines (Steve). ~~Also fixes the "cancel" answer, which today
    returns without sending anything, so no reply comes and the
    operation never finishes.~~ *Wrong: after the question,
    `startUpload()` and `startDownload()` check `isTerminating()` and
    finish the operation (`killOp()`); Steve confirmed that Stop works.
    The continuation must keep that: a "cancel" answer finishes the
    operation.* **Done** (2026-10-27):
    - `CopyOperation::askTargetFileExists()` keeps a copy of the
      existing target's info and asks through the listener, which
      returns whether the question was posted; the operation returns
      without sending anything. `FileTransferCore::answerTargetFileExists()`
      passes the answer, posted by the dialog, to the current operation,
      which ignores it unless a question is pending, and goes on in
      `onTargetFileExistsAnswer()`: overwrite, append and skip as before;
      a cancel, or an answer to an operation told to stop meanwhile
      (the connection ended), finishes it. A question that cannot be
      posted (no dialog) stops the operation, as a cancel did.
    - `startUpload()` and `startDownload()` do not finish the operation
      while a question is pending, so that it finishes only once.
    - Upload asks for the first remote file of the same name only; it
      used to ask again for each further one, which only a server
      listing a name twice could cause.
    - The dialog shows `FileExistDialog` on the GUI thread, where its
      "skip all" and "overwrite all" memory now lives alone. A result
      other than its buttons' (it could not be shown) counts as a
      cancel, not, as before, as overwrite. `FileTransferEvents` no
      longer calls the dialog at all.
  - **5d. The local delete on a pool for blocking work**, an
    `asio::thread_pool` of one thread owned by `TvnViewer`, created on
    first use (Steve: its threads start in its constructor) and kept;
    the delete holds a claim on the instance and stops on an atomic flag,
    so nothing is joined but the pool at exit. Then the file-transfer
    locks go.
    **Done** (2026-10-27):
    - `BlockingWorkPool` (`viewer-core`), owned by `TvnViewer` next to
      `NetworkThread` and declared before the instances: an
      `asio::thread_pool` of one thread in a `std::optional`, made by the
      first `getExecutor()`, under a mutex, since the first call comes
      from the network thread.
    - The operations stay free of Asio: `BlockingWorkRunner`
      (`ft-client-lib`) has `runBlocking()` and `postToNetwork()`;
      `FileTransferCapability` implements it privately, holding a claim
      on the instance (`RemoteViewerCore::claimOwner()`, public) while
      the work runs, and posting back with `postToLoop()`.
    - `LocalFilesDeleteOperation` is no longer a `Thread`: it deletes on
      the pool, posts each message and then its end to the network
      thread, and stops on an atomic flag set by `terminate()`. Its
      reports run in order, and its end comes last, so it is replaced
      only after everything it posted has run.
    - `ViewerWindow::onDestroy()` posts `terminateCurrentOperation()`, so
      closing a viewer window stops a long delete, which would otherwise
      hold the instance, and the application's exit, until it ended.
    - `ListenerContainer` (used only by file transfer) has no lock any
      more, nor do the operations' and the message processor's
      notifications. The request sender still locks the output gate,
      for step 7.
  - Left on the network thread: reading and writing the local file chunk
    by chunk, and the upload's walk to total its size.
- **Step 6, listening mode on an Asio acceptor** (agreed with Steve,
  2026-10-27): the socket is opened, bound and set to listen on the GUI
  thread, so that a port in use is reported at once, as before; only
  accepting and closing run on the network thread. `-listen` on the
  command line using the configured port, not always 5500, is a separate
  commit. *Done (2026-10-27): `-listen` uses `ViewerConfig`'s listen
  port, as the login dialog's Listen button does; the default is still
  5500, and `ConnectionListener::DEFAULT_PORT` is gone.*
  **Done** (2026-10-27):
  - `TcpAcceptor` (`viewer-core`): an `asio::ip::tcp::acceptor` whose
    accept loop runs on the network thread and gives each connection to
    a handler its creator supplies. `stop()` posts the closing and does
    not wait; once the closing has run, the handler is not called again,
    even for a connection accepted just before it. Neither `SO_REUSEADDR`
    (which Asio's acceptor constructor would set, letting another socket
    bind the same port on Windows) nor `SO_EXCLUSIVEADDRUSE` (with which
    binding the port again would fail while earlier sessions are open).
    A client that gives up before the accept is skipped; another error is
    logged and the accept retried after a second, where `TcpServer`'s
    thread silently ended, leaving the tray icon up with nothing
    listening. Unit tests on the loopback address.
  - `ConnectionListener` owns one; its handler queues the socket in an
    inbox shared with it (so the GUI may destroy the listener at any
    time) and posts `WM_USER_NEW_LISTENING`, as before. Connections are
    taken in the order accepted; `TcpServer`'s list gave the newest
    first.
  - `RemoteViewerCore::start()` and `ViewerInstance` take the
    `asio::ip::tcp::socket` itself; the raw `SOCKET`, `assign()` and
    `SocketIPv4` are gone from them. `TcpServer` is deleted.
  - `-listen` starts listening from the message loop
    (`postToMessageLoop()`), once the main window the listener posts to
    exists. It was started before `run()` created the window, and the
    listener's thread read the handle unsynchronized: a connection
    accepted in between was posted with a null handle, to the listener
    thread's own queue, where nothing read it, and waited in the list
    for the next connection's message.
- **Step 7, deletion** (agreed with Steve, 2026-10-27): the `network`
  folder stays, holding what is still used (`HostResolver`, the gates,
  `SocketException`). First the dead socket code; then the locks whose
  data is no longer shared between threads; then a table of the locks
  left, to be taken one by one. `Thread` and `WindowsEvent` stay while
  `FbUpdateNotifier` does.
  - **7a. Dead socket code** (2026-10-27): `SocketIPv4`,
    `SocketAddressIPv4`, `SocketStream`, `WindowsSocket`, `sockdefs.h`
    and `WsaStartup` deleted. The keepalive setting is now the free
    function `enableTcpKeepAlive()` (`network/TcpKeepAlive.h`), tested on
    an Asio socket. `TvnViewer` no longer starts WinSock: Asio does.
    rfb-replay's `LoopbackServer`, which used raw WinSock, runs on an
    `io_context` of its own.
  - **7b. Locks of the network side** (2026-10-27), each checked to have
    every use on the network thread (or before `start()`, on the
    caller's thread, with the post of `start()` ordering it before):
    - `RfbOutputGate` is no longer a `LocalMutex`, and its sixteen
      `AutoLock`s go: the client messages' `send()`, the file-transfer
      request sender and `ENABLE_CUT_TEXT_UTF8`. Every write is on the
      network thread: input and settings are posted events, and the
      file-transfer operations run there since step 5.
    - `RemoteViewerCore`: `m_connectLock`, `m_pixelFormatLock`,
      `m_refreshingLock`, `m_freezeLock` and `m_requestUpdateLock`. Their
      fields are touched by the event handlers and the receive loop only.
    - `CapsContainer::m_mapLock`: filled before `start()`, read on the
      network thread; the GUI has the `ConnectionInfo` snapshot.
    - Kept: `m_startLock`, since `post()` reads `m_wasStarted` on any
      thread (ordered by `start()`, but on more than one thread).
  - **7c. Locks of `win32-lib` used on the GUI thread only** (2026-10-27):
    `WindowsApplication::m_MDLMutex` (the modeless-dialog list),
    `ScaledIcons`' cache lock (shared only while the password dialog ran
    on the core thread), `DisplayRequest::m_lock`, and the `m_cs` of
    `ViewerConfig` and `ConnectionConfig`: nothing on the network or
    notifier thread reads the configuration (the core gets settings as
    posted events; the logger synchronizes itself). `ConnectionConfig`'s
    copy constructor and assignment, written only to take both locks,
    are defaulted.
  - **7d. `QueuedOutputStream`'s lock** (2026-10-27), missed in 7b: its
    writers (the gate) and its reader (`writeLoop()`) are all on the
    network thread. The core's comment that any thread may write is
    corrected.
  - **7e. From the table of remaining locks** (2026-10-27, Steve's
    choices): rfb-replay's `MemoryOutputStream` and `ReplayEventsAdapter`
    use `std::mutex` (the adapter's end-of-session `WindowsEvent` becomes
    a `std::condition_variable`); `BlockingWorkPool` starts its thread
    with `std::call_once` instead of a mutex; `ResourceLoader` is no
    longer a `Singleton<T>`: `getInstance()` returns a function-local
    static, made on first use for `GetModuleHandle(nullptr)`.
    `ViewerConfig` likewise, its `getInstance()` defined in `tvnviewer`
    (`main.cpp`), which knows the registry path (Steve: the registry is
    to go, so no more churn than that); `Singleton.h` is deleted.
  - **7f. `FileAccount`'s lock is not recursive** (2026-10-27): a
    `std::mutex`, taken by the public functions only; the rest expect it
    held (`printLocked()`, and `LogDump::flush()`, now protected).
    `acceptsLevel()`, which every log call makes on every thread, and
    `getFileName()` now take it too: they read `m_file` and `m_level`,
    which `changeLogProps()` changes on the GUI thread.
  - **7g. `RemoteViewerCore`'s interface narrowed** (2026-10-27, agreed
    with Steve):
    - Its public functions are for one thread, the consumer's (the GUI;
      rfb-replay's main thread, `copyFrameBuffer()` included). Before
      `start()` the settings change the core directly; from `start()` on
      they are posted. So `m_wasStarted` is a plain `bool`, and
      `m_startLock` goes: it never made `post()` or `stop()` atomic (each
      tested the flag and acted after releasing the lock); what makes
      them correct is that `start()` runs on the same thread.
    - `setMaxFrameBufferPixels()` becomes an argument of `start()`.
    - `CapabilitiesManager` is the extension point for the modules
      (authentication handlers, the file transfer): the `add…()`
      functions, and `postToLoop()` and `claimOwner()`, the only
      functions with other thread rules. The `getEnabled…()` functions,
      used only inside the core, are private there (the encodings one,
      unused, is gone). The core inherits it privately and gives it out
      explicitly, `getCapabilitiesManager()`, as `ViewerCoreEvents` gives
      out its adapter (Steve: the extension point shows in the code, and
      the call can take arguments or pre-conditions later), so the
      modules no longer include `RemoteViewerCore.h`.
    - Pimpl: `RemoteViewerCore` (`RemoteViewerCore.h`) is the consumer's
      interface, forwarding to `RemoteViewerCoreImpl`
      (`RemoteViewerCoreImpl.h/.cpp`, the former `RemoteViewerCore.h/.cpp`
      moved, for viewer-core only), held by `std::unique_ptr`. Its header
      keeps only Asio's executor and TCP socket; the protocol, decoders,
      coroutines and the rest of Asio stay in the implementation. File
      and line references to `RemoteViewerCore.cpp` in older notes now
      point into `RemoteViewerCoreImpl.cpp`.
    - Considered and left for later: a `std::shared_ptr` to the state
      shared with the network thread, instead of relying on the
      adapter's control block owning the core (see `start()`); Steve
      would prefer it, but not now.
  - **7h. `HostResolver`'s lock** (2026-10-27): replaced by an atomic
    flag, `m_isSettled`, set by the completion and exchanged by
    `cancel()`, which calls `GetAddrInfoExCancel` only if it was unset;
    so `cancel()` may be called any number of times. Fixes the deadlock
    described under "Host name resolution".
  - **7i. Parsing shared with `rfb-fuzz`** (2026-10-27): the parsing of
    the server messages after the security handshake moves out of the
    core into free functions, `Internal::readPixelFormat()`,
    `readServerInit()`, `readRectangleHeader()`, `readRichCursor()`,
    `readServerCutText()` and the like, with the checks on what they
    parse (`checkFrameBufferProperties()`, `checkRectangleInside()`), in
    `viewer-core/RfbParser.h`, which does not include Asio. Each takes
    the `AwaitableReader`, and a `LogWriter` where it logs. The core and
    `rfb-fuzz` both call them; what is done with the result (the frame
    buffers, the decoders, the cursor) stays duplicated until the frame
    buffer is reworked. The handshake and authentication stay in the
    core, out of `rfb-fuzz` on purpose (`testing.md`, "Duplication").
    Two changes of behaviour: the core reads the desktop name before it
    checks the frame buffer's size, and the fuzzer now decodes the
    desktop name and the cut text instead of skipping them.

Phase 3 and step 4.2 are where behaviour really changes: Phase 3 lets
the parser stop mid-message, and 4.2 changes which thread owns the
connection. They are kept apart so that a regression points to one or
the other. The largest diff is Phase 2, but it is mechanical.

### Afterwards, as features

- IPv6: resolve with `AF_UNSPEC`, and the bracketed host syntax in
  `networking.md`.
- Timeouts, and pointer coalescing in the post-to-loop path.
- TLS, then Continuous Updates with Fence. With them, a refresh (and
  the refresh after an encoding change) can take effect on an idle
  server: see Phase 4 step 2e.
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
    - ~~Channels are labelled as ‘experimental’, but they are pretty much required in our design.~~ *Not usable for the GUI-to-driver queue after all; see correction 2 below.*
    - (The use of channels is common in modern asynchronous code in .NET too.)
  - GUI requests to disconnect, which need to jump the queue, can work via Asio ‘cancellation signals’ *(emitted on the network thread, not the GUI thread; see correction 1)*
  - Channels are cleaner than locks in that we can have a single coroutine at the top driving everything for one connection 
    - That coroutine is what we called the ‘driver’ above
    - The channel has to be, in principle, unbounded, as we should not lose messages when the network is slow *(Asio's channels cannot be; see correction 2)*
  - The ‘weak reference’ mentioned above is only conceptual
    - The implementation need not use an actual `std::weak_ptr` or anything like that
    - ~~`asio::cancellation_signal` and `asio::concurrent_channel` are *already thread-safe* so the GUI thread could just trigger/send on them~~ *Wrong for `cancellation_signal`; see correction 1.*
    - An already-dead connection would just ignore further cancellations and messages
  - However, `ViewerInstance` needs to wait for the driver inside `RemoteViewerCore`, before destroying shared state such as the `asio::cancellation_signal` or `asio::concurrent_channel` objects
    - `std::shared_ptr` could be used to hold those objects so the coroutine's lifetime can exceed that of `RemoteViewerCore`
    - However, we don't want crashes when shutting down the program, so there needs to be a final synchronous wait for Asio's executor

### Corrections and decisions (2026-10-25)

Reviewed with an agent after the section above was written. The
corrections were checked against Asio's documentation and source (Boost.Asio
`develop`, which tracks standalone Asio); some answers in
[the Asio Q&A](cpp-asio.md) are wrong on these points and are marked there.

1. **`cancellation_signal::emit()` is not thread-safe.** It calls the
   installed cancellation handler synchronously, on the calling thread,
   through a plain pointer with no locking. Asio's documentation says
   the thread-safety rules are "as if calling a member function on the
   target operation's I/O object", and for composed operations (which a
   `co_spawn`ed coroutine is) the emit must not run concurrently with the
   operation's intermediate completion handlers. Those run on the network
   thread, so the GUI must *post* a small task that does the `emit()`.
2. **Asio's channels have a fixed capacity.** `max_buffer_size` is the
   maximum number of buffered messages, and the default 0 means no
   buffering at all: a send waits for a receiver (a rendezvous).
   `try_send` fails when the buffer is full and no receive is waiting,
   and the GUI cannot use `async_send` because it cannot wait. So:
   - The GUI-to-driver queue is `asio::post` itself, which is unbounded,
     ordered and thread-safe. The posted task appends to a plain
     `std::deque` that only the network thread touches, and wakes the
     writer (a non-concurrent channel of capacity 1, or a timer that is
     cancelled, used as an event).
   - Our own deque also allows coalescing: a pointer move can replace
     the queued one before it when the button state is unchanged.
   - Disconnect requests are posted the same way (correction 1), so both
     paths from the GUI use one mechanism.
   - (Aside: in Boost.Asio `develop` the channel classes have moved out
     of `experimental`; the old names remain as aliases.)
3. **`operator||` on awaitables waits for one operand to *succeed*.** It
   is built on `wait_for_one_success`: when one operand throws, the
   other is *not* cancelled, and the operator keeps waiting for it. With
   `read_task || write_task`, a disconnect seen by the reader would then
   hang until the writer finishes, which it never does while it waits
   for GUI events. The driver should use `make_parallel_group(...)`
   with `wait_for_one()`, which cancels the others when any operand
   completes, successfully or not. (`operator&&` is `wait_for_one_error`:
   it cancels on an exception but waits for both on success.)

Decisions (Steve, 2026-10-25):

- **The queue carries typed events.** The GUI thread never encodes RFB.
  Events are encoded on the network thread, into the same output buffer
  as the core's own messages (FramebufferUpdateRequest, Fence replies,
  file-transfer data), so everything sent on a connection has one order.
- **Unwinding is cooperative.** When the connection dies, a parser
  suspended in a read is resumed with an exception thrown from the
  reader awaitable's `await_resume`, rather than having its coroutine
  frame destroyed. This matches the `IOException` behaviour of today's
  code, and `catch` handlers keep running.
- **Ownership by `std::shared_ptr`.** The state shared between the GUI
  side and the driver is held by a `std::shared_ptr`. The GUI side holds
  one reference and the driver coroutine receives the other *by value*
  when it is spawned; each side then borrows from its own reference, so
  the reference count is not touched per operation. When a window
  closes, its GUI objects can go at once while the driver finishes
  unwinding; no wait is needed. Several connections share one network
  thread, and the only synchronous wait is at *process* exit, joining
  that thread. Made precise (Steve, 2026-10-26), after a first
  implementation of step 2c without shared ownership:
  - **What is shared is the whole `ViewerInstance`**, not the core's
    state alone. The driver calls into GUI-side objects the instance
    owns: the adapter, the authentication handler, and
    `FileTransferCapability`, whose coroutines can be suspended
    mid-reply with references into it. One control block covers them
    all, and the core itself.
  - **The core sees only the adapter.** `RemoteViewerCore::start()`
    takes a `std::shared_ptr<CoreEventsAdapter>`, made with the aliasing
    constructor from the instance's own pointer
    (`enable_shared_from_this`), so it shares the instance's control
    block. The core has no dependency on the GUI; its precondition is
    that the adapter's owner keeps the core, and whatever is registered
    with it, alive.
  - **No cycle: each side holds a claim while it is active.** The
    reference is not a member of the core. The driver holds it from
    `start()` until its completion has run, and each piece of work
    posted for the core holds one until it has run, so the reference
    count also answers "is anything left on the network thread?" The
    window holds one for itself until it is destroyed (`WM_NCDESTROY`),
    the usual Win32 "window deletes itself" pattern. The instance lives
    as long as its window or its connection, whichever ends later.
  - **The instance is deleted on the GUI thread, at the top of its
    message loop.** The last reference is usually the driver's, dropped
    on the network thread, but the instance owns windows. Its deleter
    therefore posts the deletion to the GUI thread, where it runs from
    the application's own message loop, never from the nested loop of
    a dialog, message box or menu, so no window procedure of the
    instance is on the stack. (Polling for finished instances from a
    timer had to exclude, by hand, an instance whose password dialog
    was open; the top of the loop makes that unnecessary.)
  - **`ViewerCollector` only implements cooperative termination.** It
    owns nothing: it keeps a `std::weak_ptr` to each instance, so that
    if the application ends with connections still open, it can stop
    them, and a count of instances not yet deleted, which keeps the
    application alive until their deletions have run, before the
    network thread is joined.
  - **The adapter as an object of its own** (Steve): the
    adapter is called on the network thread, and must not touch the
    window's other members, which belong to the GUI thread. An explicit
    private interior object, rather than `ViewerWindow` inheriting
    `CoreEventsAdapter`, documents that, and denies such access by
    default. **Done** (2026-10-26): `ViewerCoreEvents`, a member of
    `ViewerWindow` and also the password prompt of
    `ViewerVncAuthHandler`. It is not nested in `ViewerWindow`, as a
    nested class has access to the enclosing class's members; it holds no
    reference to the window, only its `HWND`, the desktop window (for the
    frame buffer events) and the file transfer. It keeps what the
    connection reports under its own lock. ~~Its GUI-thread side is
    private, for `ViewerWindow` (friend).~~ *Its GUI-thread side is
    public, documented as for `ViewerWindow` only: a friend would have
    opened its data to the window too. It inherits `CoreEventsAdapter`
    privately, with private events, so only the core can call them,
    through `getAdapter(owner)`, which makes the aliasing pointer.* At
    `WM_NCDESTROY` the window detaches it, so nothing is posted to a
    handle that may be reused.
  - **Which references are shared** (Steve, 2026-10-26). A
    `std::shared_ptr` is a claim held by an *activity* that can outlive
    the owner's own reasons to exist: the running connection, each piece
    of posted work, the window until `WM_NCDESTROY`. Each ends with its
    activity, so no cycle forms. Members of the same owner that refer to
    each other (the authentication handler to `ViewerCoreEvents`, the
    core to its capabilities) use plain references: they are made and
    destroyed together, and a `std::shared_ptr` among them, sharing the
    owner's control block, would make the owner own itself and never be
    deleted. New code, such as file transfer on the loop (step 5),
    follows the same rule.
- For this to be safe, the driver never blocks on the GUI (it only posts
  to it), and cancellation always completes a pending password or
  certificate prompt with `operation_aborted` (Phase 4 step 3).
- **The framebuffer stays shared**, behind its mutex (Phase 4 step 4).
  It is the exception to "connection state is private to the driver".
- **TLS**: an Asio socket allows one read and one write in flight at
  once, and a TLS layer needs reads that can trigger writes (key updates,
  session tickets). Whether the writer stays one drain loop that the
  read side also feeds, or reading and writing become independent, can
  be decided after Phase 4; it is orthogonal to the rest.
- **The interface of `RemoteViewerCore` is narrowed before the rewrite**,
  as far as is reasonable, at the end of Phase 1 (step 6).

### The boundary between the core and Asio (2026-10-26)

Decided with Steve after `Task<T>` was written (Phase 1 step 4).

**The protocol core uses our `Task<T>` throughout, also after Phase 4**:
the handshake, the message loop, the decoders and the file-transfer
reply parser. `asio::awaitable` appears only in the driver and the
transport. Asio's headers, which are heavy, stay out of the core, and
the fuzzers and replay tests drive the whole core without Asio.

**Asio enforces this boundary itself.** Inside an `asio::awaitable`,
`co_await` accepts only another `asio::awaitable`, an Asio asynchronous
operation, or Asio's `this_coro::` tokens (`awaitable_frame_base::await_transform`
has no generic overload). So the driver cannot `co_await` a `Task`; it
drives the core from outside. This is unlike .NET, where any type
following the awaitable pattern can be awaited anywhere. Resumption
itself is not restricted: any `std::coroutine_handle` can be resumed
from anywhere, including from an Asio completion handler. The
restriction is on what an Asio coroutine may await.

**How the driver runs the core:**

1. The core is a chain of `Task`s, started by the driver. It runs until
   its leaf awaitable (the reader) cannot proceed, stores the handle of
   the coroutine awaiting it, and suspends; control returns to the
   driver, which last resumed the chain.
2. The leaf states what it is waiting for:
   - **more bytes**, the common case;
   - **a request** to the host: "need the password", later "ask
     whether to trust this certificate" and "start TLS now" (VeNCrypt,
     partway through the RFB handshake).
3. The driver, an `asio::awaitable` on the network thread, serves the
   need with Asio (`async_read_some`; a GUI-completed operation; a TLS
   handshake on the transport), puts the result into the reader, and
   resumes the stored handle.
4. When the driver's operation fails or is cancelled, it puts the error
   into the reader instead, and resumes the core: `await_resume` throws,
   and the chain unwinds by itself (cooperative unwinding, above). The
   core needs neither Asio's executors (it only runs when the driver
   resumes it, on the network thread) nor its cancellation slots.

The write side needs no coroutines: encoding a client message appends to
the connection's output buffer synchronously, and only the driver's
writer awaits the socket.

An alternative considered and rejected: the handshake as an
`asio::awaitable` at driver level, calling `Task`-based helpers for each
part. Less machinery, but the handshake could then be tested only with
Asio. With requests, the key operations are also explicit at the driver
level, and the core has one kind of suspension point.

Where the earlier sections describe the TLS negotiation as `co_await
tls.async_handshake(...)` and `co_await askUserToTrust(cert)` ("TLS",
under "Features"), read those as requests from the core that the driver
serves this way.

Reference information:

  - [Q&A on the Asio C++ library](cpp-asio.md)
  - [Q&A on C++20 coroutines](cpp-coro.md)
