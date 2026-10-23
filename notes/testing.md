# Plan: protocol regression tests and decoder fuzzing

A plan (2026-10-23) for the first automated tests: a replay harness
that runs recorded or generated RFB sessions without the GUI, and
libFuzzer targets for the rectangle decoders. Nothing here is
implemented yet. The motivation is the Region bug fixed after the /W4
work: every region came out empty and the viewer showed a black screen.

That bug did not touch the framebuffer itself: the decoders still drew
into it, but `FbUpdateNotifier` merges the updated areas into a
`Region`, which came out empty, so the GUI was never told to repaint.
So the replay tests below, which compare the final framebuffer, would
not have caught it. What would have is a unit test of `Region`, run as
a small program in `harnesses/` like the others; it is a separate item
from this plan (see "Later").

See also `portability.md` ("A command-line RFB client over
stdin/stdout", "Fuzzing") and `async-networking.md` ("Transports, and
stdin/stdout", "Order of work").

## Scope

- **In:** replay tests of whole sessions through `RemoteViewerCore`,
  and fuzzing of FramebufferUpdate messages, read by the fuzzer itself
  and passed to the real decoders.
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
- The rich cursor in `processPseudoEncoding()` was checked against
  `INT_MAX` bytes too, so a cursor of about 2 GB was allowed. It is now
  capped at a fixed 1024 × 1024 (`ViewerBufferSize::MAX_CURSOR_SIZE`)
  for the viewer and the tests alike, since real cursors are rarely
  larger than 256 × 256.

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

A console program in `harnesses/rfb-replay/`, built in all four
configurations and in `Fuzz|x64` (below), so the regression tests also
run under ASan.

```
rfb-replay [options] INPUT
rfb-replay --check CASE
  INPUT              server-to-client bytes, or - for stdin
  CASE               tests/cases/CASE: run CASE.rfb and compare with
                     CASE.out, CASE.events and CASE.ppm
  --out FILE         client-to-server bytes (default: stdout)
  --events FILE      event log (default: none)
  --ppm FILE         final framebuffer as PPM (default: none)
  --password TEXT    for VNC authentication (default: "password")
  --max-pixels N     framebuffer limit (default: 4096 × 4096)
  --timeout SECONDS  give up on a session that has not ended (default: 30)
  --log LEVEL        core log on stderr, 0 to 9 (default: 1, errors)
```

Exit codes: 0 for success or a match, 1 for a mismatch, 2 for a usage
or I/O error, 3 for a timeout. On a timeout the program exits without
destroying the core, since a looping decoder cannot be stopped and the
destructor would wait for it.

**Defaults.** Besides `start()`, with the shared flag set, the harness
calls only `setMaxFrameBufferPixels()` and registers a VNC
authentication handler. The client's bytes are then those of a viewer
with default settings: no SetPixelFormat (the framebuffer keeps the
server's format), SetEncodings with the default list (Tight first), a
full update request after ServerInit and an incremental one after each
update. An option for a client pixel format comes with the cases that
need it; the generator models whatever the harness does.

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
- **PPM.** P6, 8 bits per channel, each component `c` scaled from its
  maximum `max` in the framebuffer's pixel format as
  `(c * 255 + max / 2) / max` in integers, which the generator uses too.
  Before ServerInit the framebuffer is empty, written as
  `P6\n0 0\n255\n`. About 30 lines.
- **Event text.** A message is quoted, with backslash, double quote and
  newline written as `\\`, `\"` and `\n`, in UTF-8, one event per
  line ending in LF.

### Why replay is deterministic

Only the protocol thread writes to the output in the harness, so the
client's bytes come out the same on every run: the protocol version,
the security type, ClientInit, SetPixelFormat, SetEncodings and every
FramebufferUpdateRequest. Replay is open-loop (the server's bytes do
not react to the client's), which is fine for RFB, because a test
author knows what the client will choose. VNC authentication is
deterministic too, as the challenge is in the input.

### Test cases

The programs that run tests live in `harnesses/` (`rfb-replay`,
`rfb-fuzz`, and later `viewer-keysym-test`, which moves there whenever
convenient); the data they run on lives in `tests/`:

```
tests/
  gen/rfbgen.py          the generator library
  gen/cases.py           one function per case
  cases/NAME.rfb         server-to-client bytes (input)
  cases/NAME.out         expected client-to-server bytes
  cases/NAME.events      expected event log
  cases/NAME.ppm         expected final framebuffer
  fuzz/corpus/NAME       the server stream after the handshake
  fuzz/regressions/      fuzz inputs that found bugs
  run-tests.ps1          runs every case and compares the files
```

- The generator is plain Python 3, with only `struct` and `zlib`. It
  writes all the files of a case from one description, and the files
  are checked in, so building and running the tests needs no Python.
- **The generator is the oracle.** A case describes an image and how to
  send it (encoding, rectangles, pixel format); the generator encodes
  the image, and the image itself is the expected PPM. The expected
  client bytes are written from what the client should send. The event
  log is short enough to write by hand in the case.
- Zlib-based encodings keep their streams across rectangles and
  messages. The generator models this with one `zlib.compressobj` per
  stream and `Z_SYNC_FLUSH` after each block, as servers do.
- `python3 tests/gen/cases.py` rewrites every case's files and removes
  those of cases that no longer exist; with `--check` it writes nothing
  and lists files that differ, are missing or are stale. VNC
  authentication needs DES, which `rfbgen.py` implements itself (checked
  against the `cryptography` package) to stay within the standard
  library.
- `tests/.gitattributes` marks the generated files `-text`, so no
  checkout converts their line endings.
- **One test is one command.** `rfb-replay --check CASE` compares the
  three outputs itself, byte for byte, prints what differs (the first
  differing offset in the client bytes, the first differing line of
  events, the first differing pixel) and exits 0 or 1. Running the tests
  then needs no `diff` or `cmp`, and no Python, which is needed only to
  regenerate the cases.
- **The runner is `run-tests.ps1`**, which only runs commands and
  counts failures: `rfb-replay --check` for each `.rfb`, and the fuzz
  binary on its regression files. It takes the configuration and
  platform, so it finds the executables under `bin\PLATFORM\CONFIG`.
  No logic belongs in it, so it can be replaced without touching the
  tests.
- **CTest comes with CMake, not before.** Visual Studio ships CTest
  (4.3.1 in VS 2026), and it could run these tests today from a
  test-only `CMakeLists.txt`, but that means generating a CMake build
  directory alongside MSBuild for no gain. When CMake builds the core,
  each command in `run-tests.ps1` becomes an `add_test()` (with labels
  for replay and fuzz tests, and a `TIMEOUT`, since a decoder bug can be
  a hang), and the script goes away.

First cases, roughly in order:

1. RFB 3.8, security type None, Raw at 32 bpp: the minimal session.
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

A libFuzzer program in `harnesses/rfb-fuzz/`, built only in `Fuzz|x64`.
The aim at this stage is to show that fuzzing works in SquirrelVNC and
to cover the code that handles the most untrusted input, the decoders;
whole-session fuzzing waits for the rewrite (see "Scope").

### Input: a strict subset of RFB

The input is what a server sends after the security handshake, in a
session where the client never changes the pixel format:

1. **ServerInit:** width, height, pixel format, desktop name.
2. **Server messages,** until the input runs out.

There is no custom header, so any server stream is a valid input once
its handshake is cut off, and the generator's cases seed the corpus
directly (see "Corpus and regressions").

- **ServerInit** is checked as the core checks it: bits per pixel 8,
  16 or 32; a colour-map format replaced by the standard format of that
  depth; any other format must pass `PixelFormat::isValid()`; a desktop
  name of at most 64 KiB. The fuzzer then sets a small framebuffer
  limit, such as 256 × 256 pixels, so that runs stay fast; an input
  that asks for more ends there.
- **FramebufferUpdate** is decoded in full: the padding, the rectangle
  count, and for each rectangle its header, the check that it lies
  inside the framebuffer, and the decoder found through `DecoderStore`,
  the class the core uses. The four pseudo-encodings are handled as in
  `processPseudoEncoding()`: LastRect ends the update, DesktopSize
  resizes the framebuffers (within the fuzzer's limit), and the rich
  cursor and pointer position go to a `CursorPainter`, on which the
  fuzzer then calls `showCursor()` and `hideCursor()` after each update.
- **SetColorMapEntries, Bell and ServerCutText** are read and ignored,
  with the core's limit on cut text, so that streams containing them
  are still valid inputs.
- **Any other message type** ends the run. (The core logs it and reads
  on, which gets out of step with the stream; for the fuzzer, stopping
  is the useful choice.)
- `Exception` from any of this is caught: rejecting bad input is
  correct behaviour. ASan reports, crashes and hangs are the findings.

Everything runs on the fuzzer's thread, with new decoders for each
input, so per-decoder state (the zlib streams of Tight and ZRLE)
starts fresh. `process()` always calls `notify()`, so it needs an
`FbUpdateNotifier`. A real one that is never started will do: its
thread is created suspended, `onUpdate()` only adds to a region, and
the destructor does not wait for a thread that never ran.

### Duplication, and how to keep it honest

This reading loop copies what `RemoteViewerCore` does in
`clientAndServerInit()`, `readPixelFormat()`, `receiveFbUpdate()`,
`receiveFbUpdateRectangle()` and `processPseudoEncoding()`, rather than
moving that code out of the core into a shared class. The copy is
about a hundred lines, and the mainline code does not change for the
fuzzer. The decoders and `CursorPainter`, which are what is being
fuzzed, are the real ones.

The cost is that the checks made before a decoder runs (the rectangle
bounds, the cursor size, the frame buffer limit, the pixel format) are
tested in the copy, not in the core. To keep the two in step:

- Each copied check has a comment naming the function it mirrors.
- Every input that finds a bug also becomes a replay case: the
  generator adds a handshake in front of it, so `rfb-replay` runs it
  through the real core. A decoder bug then shows in the mainline path
  as well, and a difference between the copy and the core shows as a
  failing case.

After the asynchronous rewrite, the server-message loop is a
single-threaded coroutine that the fuzzer can call directly; the copy
goes away then, and other message types come along with it.

### Corpus and regressions

- The generator writes each case's server stream after the handshake
  to `tests/fuzz/corpus/NAME` as well. For cases in which the
  client sets a pixel format of its own, the generator writes a
  ServerInit carrying that format, since the updates are encoded in it.
  Cases using the Tight security type are left out, as their
  capability lists follow ServerInit.
- Each input that triggers a finding is minimised and checked in under
  `tests/fuzz/regressions/`, and becomes a replay case as above.
  `run-tests.ps1` runs the fuzz binary on those files (libFuzzer runs
  given files once and exits, failing on a crash), so a fix stays
  fixed.

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
   with the internal setter. Done.
2. **The framebuffer accessor** (core change 2). Done.
3. **The generator and the first cases** (`tests/gen`, `tests/cases`),
   with no C++. Done, with nine cases: a Raw session at 32 bpp, RFB
   3.3, 3.5, 3.7 and 4.1, VNC authentication succeeding and failing,
   and refused security. They have not run against the core yet; step 4
   is where they do.
4. **`harnesses/rfb-replay` and `run-tests.ps1`**, passing the first
   cases. Done: the nine cases pass in all four configurations. Then
   more cases, in the order above, each batch its own commit. Each case
   that finds a bug goes in with the fix, or just before it.
5. **The `Fuzz|x64` configuration**, with `rfb-replay` running the
   cases under ASan.
6. **`rfb-fuzz`**, seeded at first with the Hextile cases as the proof
   of concept (Hextile is small and has the most branching per byte);
   then the whole corpus.
7. **`rfb.ksy`**, whenever it is useful.

## Later

- **Unit tests** of the pure classes, starting with `Region` (whose
  clamp bug the replay tests could not have caught, as explained at the
  top), as another program in `harnesses/` run by `run-tests.ps1`.

Known issues the tests may run into, already recorded elsewhere:

- `Region` and `x11region` use 16-bit coordinates, so desktops wider
  or taller than 32767 pixels do not repaint at the far edge. The small
  framebuffers of the tests stay clear of this.
- `TightDecoder` truncates colour components when a maximum is above
  255 (a documented precondition); a case with such a format would
  show it.
