# Observations on CPU usage

Preliminary observations from the main author.  The following discussion needs uploaded profiling data backing it up.
(See "Measurements" below.)

The author often runs a stress test of the VNC client and server by watching movies over an Ethernet connection.

SquirrelVNC runs on the author's laptop, and it runs *hot* during movies.

A sampling profiler shows much time is spent on:

  - Copying memory of bitmaps
  - Calling `FrameBuffer::getBufferPtr`
  - Inside `TightDecoder::drawTightBytes`.
  - GDI `BitBlt`

One might expect the JPEG decoding to be most CPU-heavy, but it is not. 

That suggests SquirrelVNC, which inherited all this code from TightVNC, *can* be optimized.

For example:

  - Eliminating copies
  - More efficient copying loops
  - SIMD usage perhaps
  - DirectX rendering instead of DIB

## Measuring

The profiles are taken with [Very Sleepy](https://github.com/VerySleepy/verysleepy),
a sampling profiler. It counts *wall* time: a thread blocked in
`WaitForSingleObjectEx`, `GetQueuedCompletionStatus` or `GetMessage` is
sampled just like one that runs, so its own reports are dominated by
waiting threads.

`tools/cpu-usage/sleepy-cpu.py` reads the `.sleepy` file (a zip archive
holding the symbols and the call stacks of each thread, which the CSV
export lacks), drops the samples whose innermost frame is a blocking
system call, and prints:

- the time of each thread, not waiting and in total;
- the exclusive and inclusive time of each function;
- with `--lines`, the self time of each source line;
- with `-f REGEX`, the callers, callees and source lines of the
  functions that match.

`-t TID` keeps one thread only; `--wait` replaces the pattern of
waiting functions; `--keep-waits` keeps them. For example:

```
python tools/cpu-usage/sleepy-cpu.py capture.sleepy --lines -f drawTightBytes -f "^memcpy$"
```

What the filter cannot do: a user-mode sampler cannot tell a thread
running inside a system call from one blocked inside it. Only the
obvious waits are dropped; time in `NtGdiBitBlt`, `NtUserBeginPaint`,
`NtUserInvalidateRect` and the like is kept, and may be partly waiting
(for win32k's lock, or for DWM). To separate the two, use the Windows
Performance Recorder and Analyzer (WPR/WPA), whose "CPU Usage (Precise)"
view shows when each thread waited and why.

## Measurements

### 2026-10-26: a movie, Tight with JPEG

Profile `capture-20261026a.sleepy` (kept outside the repository, in the
author's `SquirrelVNC-profiles`), 45 s of a movie, viewer from the
`async-networking` branch at `cda3926` (Phase 4 step 2e: network thread
on Asio, `FbUpdateNotifier` still present).

Of 378 thread-seconds sampled, 24.6 s were not waiting:

| Thread | CPU (s) | Spent in |
|---|---:|---|
| network (`RemoteViewerCore::readLoop`) | 13.5 | `TightDecoder::processJpeg`: 10.4 |
| GUI (`WindowsApplication::run`) | 8.5 | painting: 6.3 |
| `FbUpdateNotifier` | 2.5 | `InvalidateRect`: 1.9; copy into the DIB: 0.4 |

**The network thread.** `processJpeg` takes 10.4 s, of which:

| | s |
|---|---:|
| `TightDecoder::drawTightBytes` (in all) | 5.1 |
| — its own loop | 2.5 |
| — `FrameBuffer::getBufferPtr` | 1.85 |
| — `memcpy` | 0.8 |
| `TightDecoder::transformArray` | 1.9 |
| `JpegDecompressor::decompress` (libjpeg-turbo) | 3.0 |

So the JPEG decoder itself, with its AVX2 colour conversion, IDCT and
upsampling, costs 3.0 s, and putting its output into the frame buffer
costs 7 s:

- `transformArray()` converts the decoder's RGB into B, G, R, 0, one byte
  at a time, into a new vector.
- `drawTightBytes()` then copies each pixel on its own:
  `fb.getBufferPtr(x + i % width, y + i / width)`, a division, a modulo
  and an out-of-line call for every pixel, and a `memcpy` of 4 bytes
  (`TightDecoder.cpp:348`–`349`, 5.1 s between them).

`DecoderOfRectangle::copy()`, from the rectangle buffer into the shared
frame buffer, is 0.4 s, and `SetEvent` (waking the notifier) 0.3 s.

**The GUI thread.** Of 6.3 s in `PaintWindow::wndProc`: `BitBlt` 2.4 s,
`BeginPaint` 2.2 s, `EndPaint` 1.2 s. That `BeginPaint` and `EndPaint`
together cost more than the `BitBlt` is suspicious; together with the
notifier's 1.9 s in `InvalidateRect` (one call per rectangle, from
another thread), it could be contention on win32k's lock, or many small
paints. Not known from this profile: see "Measuring" for WPA.

### 2026-10-28: a WPR trace, scaled down

Trace `CAITSITH.10-28-2026.14-08-09.etl` (Windows Performance Recorder,
outside the repository with the other profiles), of a Release build
showing the remote screen scaled down, about 35 s of updates. Read with
TraceProcessor (the .NET library behind WPA): context switches with the
stacks of each wait and of the thread that ended it, and CPU samples.

- **Paints:** 61,852 `WM_PAINT`s on the GUI thread, and 61,842 wake-ups
  of its message pump by the notifier's `InvalidateRect`: one paint per
  rectangle, about 1,700 a second, with no merging. That was the
  problem.
- **Lock contention on win32k's lock, small:** the GUI thread waited on
  it 2,823 times in `BeginPaint` (45 ms in all, most waits under
  0.1 ms), 55 ms in `EndPaint`, about 0.5 s in all its calls; nearly
  every wait ended by the notifier leaving `InvalidateRect`. The
  notifier waited 0.3 s on it in `InvalidateRect`.
- **`BeginPaint` cost CPU, not waiting:** 1.7 s, in setting up the DC
  and its clip region (`GetDCEx`, `GreSelectVisRgn`, `GreCombineRgn`):
  about 28 µs a call, times 62k calls.
- **`StretchBlt` with `HALFTONE`:** 7.5 s of the GUI thread's 14.2 s,
  6.2 s of it in software halftoning; about 120 µs a paint.
- **The core thread:** 3.5 s in `SetEvent`, waking the notifier about
  90k times (about 39 µs each, probably dear because the sandbox is a
  virtual machine), and 0.7 s waiting for `m_fbLock` in
  `DecoderOfRectangle::copy()`, held by the notifier across
  `InvalidateRect`.
- The trace itself costs: win32k's `EtwTrace*` frames, about 0.9 s on
  the GUI thread.

Hence the rework of 2026-10-29 (`concurrency.md`, "A framebuffer
update, end to end"): damage reported once per FramebufferUpdate,
merged, and invalidated on the GUI thread. The scaling cost per paint
remains, but paints are now few. To be measured again.

### 2026-10-29: after the rework, scaled down

Trace `CAITSITH.10-29-2026.08-01-15.etl` (about 60 s, scaled down), of
the build painting from the core's frame buffer under its lock:

- Paints are few: the GUI thread woke about 3,500 times, 40 to 60
  paints a second.
- But each paint scales the whole view with `HALFTONE`, about 12 ms:
  27.9 s of the GUI thread's CPU, 2.7 s of it GDI clearing a temporary
  bitmap on each call. The clip region does not limit the halftoning.
- The network thread waited for the frame buffer's lock 1,187 times,
  11.6 s in all (up to 25 ms each), all in `TightDecoder::processJpeg()`
  and all ended by the paint: decoding stalled about a fifth of the
  time. The GUI thread never waited for it.
- The network thread's 38 s of CPU is mostly the JPEG path (libjpeg,
  `drawTightBytes()`, `transformArray()`): items 1 and 2 below.

So the decoders no longer take the lock (Steve's proposal), only the
replacement of the frame buffer does; and the paint is to scale only
the part around `rcPaint`. Steve keeps `HALFTONE` for its quality: the
cost is CPU, not lag, and unscaled full screen (his usual use) takes
GDI's plain copy (`COLORONCOLOR`, equal sizes).

### Copies per pixel today

1. The decoder writes into the rectangle buffer (`m_rectangleFb`) —
   for Tight JPEG, after the decoder's own buffer and `transformArray()`'s.
2. `DecoderOfRectangle::copy()` copies it into the shared frame buffer.
3. The notifier copies that into the window's DIB
   (`DesktopWindow::updateFramebuffer()`).
4. `WM_PAINT` blits the DIB to the window.

Since 2026-10-29, copy 2 is gone: the decoders draw into the shared
frame buffer, taking `m_fbLock` only around each write (never across a
network read), and `m_rectangleFb` is removed.

## What to do

In order of gain for the effort:

1. **Decode JPEG straight into the frame buffer.** libjpeg-turbo writes
   B, G, R, X itself (`JCS_EXT_BGRX` in the classic API, `TJPF_BGRX`
   with a row pitch in TurboJPEG), into row pointers into the frame
   buffer. That removes `transformArray()`, `drawTightBytes()` and the
   intermediate vector from the JPEG path, about 7 s of the 10.4 above;
   the conversion moves into the AVX2 code that runs anyway. It is
   item 1 of "What the TurboJPEG API gives" in `image-decoding.md`.
   ~~First, `m_isCPixel` must check the shifts too: it checks only a depth
   of 24, 32 bits per pixel and maxima of 255, while `transformArray()`
   assumes red at shift 16 and blue at 0, so a format with red at 0 is
   drawn with red and blue swapped.~~
   The condition was right: the Tight specification sends compact 3-byte
   pixels (red, green, blue) for that format whatever its shifts, so
   checking the shifts would have read 4 bytes where the server sends 3.
   The conversions assumed red at shift 16; they now use the format's
   shifts (2026-10-29, test case `tight-bgr888`). Writing BGRX straight
   from libjpeg-turbo is right only for red at 16 and blue at 0; another
   format needs the conversion.
2. **Copy by rows, not by pixels,** in the other decoders' loops: Tight's
   copy, palette and gradient filters and `drawJpegBytes()`, and ZRLE
   (`ZrleDecoder.cpp:361`), which has the same `i % width`, `i / width`
   pattern. Raw and Hextile already read whole rows.
3. **Decode into the DIB** (Phase 4 step 4 of `async-networking.md`,
   reworked): the frame buffer's memory comes from a DIB section the GUI
   allocates, ~~the local cursor is drawn at paint time instead of into
   the pixels,~~ and `m_rectangleFb`, `FbUpdateNotifier` and copies 1 to 3
   above go. Only the blit remains.
   Decided with Steve (2026-10-29): the core keeps allocating the frame
   buffer for now, and the GUI paints it with `StretchDIBits()`; memory
   from a GUI-provided DIB section comes later. The server's cursor is
   not drawn at all, at paint time or otherwise: it is shown as a real
   Windows cursor (done 2026-10-29), unscaled, and a position the server
   moves its cursor to (PointerPos) is not shown.
4. **Find out what the paint calls cost** (WPA), before changing how
   updates are invalidated or painted. Painting at a matching depth, 1:1,
   is GDI's fast path; `StretchBlt` or a depth conversion is much slower.
   Beyond that is Direct2D or Direct3D: a separate project.
   *Measured 2026-10-28 (above), and done 2026-10-29 with item 3: the
   decoders draw into the core's frame buffer, the GUI paints it with
   `StretchDIBits()` under its lock, and `FbUpdateNotifier`, the DIB and
   copies 2 and 3 are gone. Still to do: items 1 and 2, then profile
   again. The paint still scales the whole view and relies on the clip
   region; if halftoning stays dear, give `StretchDIBits()` only the part
   around `rcPaint`, with a margin of a pixel or two.*

### Does removing `FbUpdateNotifier` cost performance?

No. Decoding has always been on the core (now network) thread; the
notifier runs in parallel only the copy into the DIB (0.4 s here) and
the `InvalidateRect` calls. Once the decoders write into the DIB, that
copy does not exist, so there is nothing left to parallelize. But the
removal should come after items 1 and 2 and together with 3, rather
than with the copy of step 4 as first planned, which would move the
notifier's copy onto the network thread. Decided with Steve
(2026-10-26): Phase 4 goes on with steps 5 to 7 first.
