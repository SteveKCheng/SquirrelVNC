# Plan: protocol regression tests and decoder fuzzing

A plan (2026-10-23) for the first automated tests: a replay harness
that runs recorded or generated RFB sessions without the GUI, and
libFuzzer targets for the rectangle decoders. Nothing here is
implemented yet. The motivation is the Region bug fixed after the /W4
work: every region came out empty and the viewer showed a black screen,
which any test that compared a framebuffer would have caught.

See also `portability.md` ("A command-line RFB client over
stdin/stdout", "Fuzzing") and `async-networking.md` ("Transports, and
stdin/stdout", "Order of work").

## Scope

- **In:** replay tests of whole sessions through `RemoteViewerCore`,
  and fuzzing of the decoders called directly.
- **Out, for now:** fuzzing whole sessions. `RemoteViewerCore` runs a
  protocol thread and an `FbUpdateNotifier` thread, so coverage is not
  deterministic, and a single-threaded mode would be thrown away by the
  asynchronous rewrite. Once the rewrite makes the core single-threaded
  and sans-I/O, a whole-session target becomes easy; add it then.
- **Out:** tests of the GUI.

## What the code already provides

The boundary between protocol and GUI already exists; the test
programs need no new abstraction to drive the core.

- **Streams.** `RemoteViewerCore::start(RfbInputGate *, RfbOutputGate *,
  CoreEventsAdapter *, bool)` accepts any `InputStream`/`OutputStream`;
  `TcpConnection::connect()` skips the socket when it is given gates.
- **Events.** Everything the core tells the GUI goes through
  `CoreEventsAdapter`, which `ViewerWindow` implements.
- **The password.** `VncAuthenticationHandler::getPassword()` is pure
  virtual; `tvnviewer/ViewerVncAuthHandler` implements it with a modal
  dialog. A test subclass returns a fixed password. (The dialog runs on
  the protocol thread today; the rewrite turns this call into a request
  to the GUI thread.)
- **Decoders.** `RemoteViewerCore::init()` registers all of them, so a
  test program gets the same set as the viewer.
- **Linking.** On Windows the test programs link `core-lib` and
  `win32-lib` like `viewer-keysym-test`; file transfer comes along but
  costs nothing.

An unwritten contract matters for any new stream: `InputStream::read()`
must throw at end of input rather than return 0, because
`DataInputStream::readFully()` loops until it has every byte.
`ByteArrayInputStream` and `SocketIPv4::recv()` both throw. Document
this as a post-condition of `read()` when the class is next touched.

## Changes to the core

Both are small and come before the test programs.

### 1. A limit on framebuffer size [SECURITY]

`ViewerBufferSize::fitsFrameBuffer()` allows up to `INT_MAX` bytes per
buffer, and `setFbProperties()` allocates two (`m_frameBuffer` and
`m_rectangleFb`). So the 12 bytes of a ServerInit, a DesktopSize
rectangle or a SetPixelFormat can make the viewer commit about 4 GB: a
denial of service by a malicious server, and an out-of-memory "crash"
on almost every libFuzzer run.

- Add a limit in pixels, checked in `setFbProperties()`, which every
  resize path goes through (ServerInit, `updatePixelFormat()`, the
  DesktopSize pseudo-encoding).
- Default: 16 × 8 × 1024 × 1024 = 134,217,728 pixels, which holds four
  8K screens (7680 × 4320 each, 132,710,400 in all). At 32 bpp that is
  512 MiB per buffer. A 32-bit build may fail to allocate that much,
  which already fails cleanly through `FrameBuffer::setProperties()`.
- Set it from C++ only, with a setter on `RemoteViewerCore` called
  before `start()`. No user setting for now. The test programs set a
  small limit, such as 4096 × 4096.
- The rich cursor in `processPseudoEncoding()` is checked against
  `INT_MAX` bytes too, so a cursor of about 2 GB is allowed. Cap it in
  the same commit, to the framebuffer limit or to a much smaller fixed
  size (real cursors are rarely larger than 256 × 256).

### 2. Reading the framebuffer after a session

The final framebuffer cannot be read reliably today. `m_frameBuffer` is
private, and the only access is `onFrameBufferUpdate()`, which the
notifier thread calls after merging updates; it may never report the
last ones before `stop()` ends that thread.

- Add an accessor that copies the framebuffer under `m_fbLock`, such as
  `void copyFrameBuffer(FrameBuffer *dst) const`.
- The cursor is drawn into the framebuffer and removed again while the
  notifier holds `m_fbLock`, so a copy taken under the lock never
  contains it.

## ① The replay harness: `rfb-replay`

A console program in `tests/rfb-replay/`, built in all four
configurations and in `Fuzz|x64` (below), so the regression tests also
run under ASan.

```
rfb-replay [options] INPUT
  INPUT              server-to-client bytes, or - for stdin
  --out FILE         client-to-server bytes (default: stdout)
  --events FILE      event log (default: none)
  --ppm FILE         final framebuffer as PPM (default: none)
  --password TEXT    for VNC authentication (default: "password")
  --max-pixels N     framebuffer limit (default: 4096 × 4096)
```

Logs go to stderr.

- **Input.** Read the whole of INPUT into memory and wrap it in
  `ByteArrayInputStream`. This sidesteps two problems: Windows anonymous
  pipes do not support overlapped I/O, and `stop()` cannot unblock a
  read on stdin. A streaming stdin, for live use behind `socat`, can
  come later; it must throw at end of input (see above).
- **Output.** An `OutputStream` over a file, or stdout set to binary
  with `_setmode(_O_BINARY)`.
- **Adapter.** A `CoreEventsAdapter` that writes one line per event
  coming from the protocol thread, and signals a `WindowsEvent` when
  the session ends:

  ```
  established
  connected
  bell
  cut-text "..."
  disconnect "End of stream reached"
  error "Error in protocol: incorrect size of rectangle"
  auth-error "..."
  ```

  Framebuffer updates and property changes come from the notifier
  thread at times that vary between runs, so they are not logged; the
  PPM covers their result.
- **Ending.** `waitTermination()` waits for the notifier first, and the
  notifier runs until `stop()`. So `main()` waits for the adapter's
  end-of-session event, then calls `stop()` and `waitTermination()`,
  then copies the framebuffer and writes the PPM. The exit code says
  whether the program itself failed; whether the session went as
  expected is for the comparison.
- **PPM.** P6, 8 bits per channel, each component scaled from the
  framebuffer's pixel format. About 30 lines.

### Why replay is deterministic

Only the protocol thread writes to the output in the harness, so the
client's bytes come out the same on every run: the protocol version,
the security type, ClientInit, SetPixelFormat, SetEncodings and every
FramebufferUpdateRequest. Replay is open-loop (the server's bytes do
not react to the client's), which is fine for RFB, because a test
author knows what the client will choose. VNC authentication is
deterministic too, as the challenge is in the input.

### Test cases

```
tests/
  gen/rfbgen.py          the generator library
  gen/cases.py           one function per case
  cases/NAME.rfb         server-to-client bytes (input)
  cases/NAME.out         expected client-to-server bytes
  cases/NAME.events      expected event log
  cases/NAME.ppm         expected final framebuffer
  run-tests.ps1          runs every case and compares the files
```

- The generator is plain Python 3, with only `struct` and `zlib`. It
  writes all four files of a case from one description, and the files
  are checked in, so building and running the tests needs no Python.
- **The generator is the oracle.** A case describes an image and how to
  send it (encoding, rectangles, pixel format); the generator encodes
  the image, and the image itself is the expected PPM. The expected
  client bytes are written from what the client should send. The event
  log is short enough to write by hand in the case.
- Zlib-based encodings keep their streams across rectangles and
  messages. The generator models this with one `zlib.compressobj` per
  stream and `Z_SYNC_FLUSH` after each block, as servers do.
- `run-tests.ps1` runs `rfb-replay` on each `.rfb` and compares the
  three outputs byte for byte, printing which ones differ. CTest can
  replace it when CMake arrives.

First cases, roughly in order:

1. RFB 3.8, security type None, Raw at 32 bpp: the minimal session,
   and the one that would have caught the Region bug.
2. VNC authentication: success, and failure with a reason string.
3. RFB 3.3 and 3.7, which negotiate security differently.
4. Each encoding: CopyRect, RRE, Hextile (every subencoding, and a
   background carried from one tile to the next), ZRLE, Tight (fill,
   palette, gradient, JPEG, basic compression with each zlib stream,
   stream reset).
5. 8 bpp and 16 bpp, both byte orders; a server format different from
   the client's.
6. Pseudo-encodings: DesktopSize, LastRect, rich cursor, pointer
   position.
7. Bell, server cut text, and the UTF-8 clipboard extension.
8. Errors the viewer must reject cleanly: a rectangle outside the
   framebuffer, an invalid pixel format, an oversized desktop name, a
   framebuffer above the limit, input that ends mid-message.

## ② Decoder fuzzing: `rfb-fuzz`

A libFuzzer program in `tests/rfb-fuzz/`, built only in `Fuzz|x64`.
The aim at this stage is to show that fuzzing works in SquirrelVNC and
to cover the code that handles the most untrusted input; whole-session
fuzzing waits for the rewrite (see "Scope").

### Targets

One target per decoder, selected at build time or by the first input
byte: Raw, CopyRect, RRE, Hextile, Tight (including JPEG) and ZRLE,
each called through `DecoderOfRectangle::process()`. Rich cursors and
pointer positions are not decoders: `processPseudoEncoding()` reads
the cursor bytes and hands them to `CursorPainter`, whose
`setNewCursor()`, `updatePointerPos()`, `showCursor()` and
`hideCursor()` a separate target calls directly.

Each run:

1. Reads a short header from the input: the pixel format (bpp 8, 16 or
   32 and byte order, mapped to valid formats), and the framebuffer
   size, bounded to something small such as 256 × 256.
2. Makes a new decoder, so per-decoder state (the zlib streams of
   Tight and ZRLE) starts fresh, and two framebuffers of that size.
3. Repeatedly reads a rectangle (bounded to the framebuffer) and calls
   `process()` on the rest of the input, until the input runs out.
   Several rectangles per input exercise the state carried between
   them.
4. Catches `Exception`: rejecting bad input is correct behaviour. ASan
   reports, crashes and hangs are the findings.

Everything runs on the fuzzer's thread. `process()` always calls
`notify()`, so it needs an `FbUpdateNotifier`. A real one that is never
started will do: its thread is created suspended, `onUpdate()` only
adds to a region, and the destructor does not wait for a thread that
never ran. One notifier can live for the whole process.

### Corpus and regressions

- The seed corpus comes from the generator: the rectangle data of the
  replay cases, rewritten in the fuzz target's input format.
- Each input that triggers a finding is minimised and checked in under
  `tests/rfb-fuzz/regressions/TARGET/`. `run-tests.ps1` runs the fuzz
  binary on those files (libFuzzer runs given files once and exits), so
  a fix stays fixed.

### The `Fuzz|x64` configuration

MSVC has supported AddressSanitizer and libFuzzer natively since
VS 2022, and neither needs a desktop.

- `core-lib`, `win32-lib`, `rfb-replay` and `rfb-fuzz` get a `Fuzz`
  configuration, set up in `Directory.Build.props`: an optimised build
  with debug information, `/fsanitize=address`, and coverage
  instrumentation (`/fsanitize-coverage=...`) for the libraries.
  `rfb-fuzz` links with `/fsanitize=fuzzer`. The exact flags will be
  settled when this is built.
- The vcpkg libraries (zlib-ng, libjpeg-turbo) stay uninstrumented.
  They are C, so the STL annotation mismatch checks do not apply.
- LeakSanitizer does not exist on Windows; leak checking waits for the
  Linux build (`portability.md`).

## Viewing the files: Kaitai Struct (optional)

An `rfb.ksy` describes both directions of the stream, with parameters
for what the other direction negotiated (the version, the security
type, Tight capabilities, the bpp in effect). Compressed data stays as
opaque byte blocks, since Kaitai's `process: zlib` inflates each block
separately and RFB's zlib streams run across blocks.

- With it, any case or recording from a real server opens in the
  Kaitai Web IDE or `ksv`. Being written separately from the
  generator, it also checks the generator against the specification.
- It is not used to write the files. Kaitai 0.11 can serialize in
  Python, but it still needs every length and count filled in by hand,
  and it cannot carry zlib state across blocks, so it saves nothing
  over `struct`.
- Only editing `rfb.ksy` needs the compiler (a Java runtime, or the
  npm build). The generated Python parser is checked in, so viewing
  needs only `pip install kaitaistruct`.
- Nothing else depends on it; if it proves more trouble than it is
  worth, the generator and the tests are unchanged.

## Order of work

Each step is a commit, or a few.

1. **[SECURITY] The framebuffer and cursor limits** (core change 1),
   with the internal setter.
2. **The framebuffer accessor** (core change 2).
3. **The generator and the first cases** (`tests/gen`, `tests/cases`),
   with no C++.
4. **`rfb-replay` and `run-tests.ps1`**, passing the first cases; then
   more cases, in the order above, each batch its own commit. Each case
   that finds a bug goes in with the fix, or just before it.
5. **The `Fuzz|x64` configuration**, with `rfb-replay` running the
   cases under ASan.
6. **`rfb-fuzz` with one target** (Hextile is small and has the most
   branching per byte) as the proof of concept; then the others.
7. **`rfb.ksy`**, whenever it is useful.

Known issues the tests may run into, already recorded elsewhere:

- `Region` and `x11region` use 16-bit coordinates, so desktops wider
  or taller than 32767 pixels do not repaint at the far edge. The small
  framebuffers of the tests stay clear of this.
- `TightDecoder` truncates colour components when a maximum is above
  255 (a documented precondition); a case with such a format would
  show it.
