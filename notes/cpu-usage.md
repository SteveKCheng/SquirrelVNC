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

### Copies per pixel today

1. The decoder writes into the rectangle buffer (`m_rectangleFb`) —
   for Tight JPEG, after the decoder's own buffer and `transformArray()`'s.
2. `DecoderOfRectangle::copy()` copies it into the shared frame buffer.
3. The notifier copies that into the window's DIB
   (`DesktopWindow::updateFramebuffer()`).
4. `WM_PAINT` blits the DIB to the window.

## What to do

In order of gain for the effort:

1. **Decode JPEG straight into the frame buffer.** libjpeg-turbo writes
   B, G, R, X itself (`JCS_EXT_BGRX` in the classic API, `TJPF_BGRX`
   with a row pitch in TurboJPEG), into row pointers into the frame
   buffer. That removes `transformArray()`, `drawTightBytes()` and the
   intermediate vector from the JPEG path, about 7 s of the 10.4 above;
   the conversion moves into the AVX2 code that runs anyway. It is
   item 1 of "What the TurboJPEG API gives" in `image-decoding.md`.
   First, `m_isCPixel` must check the shifts too: it checks only a depth
   of 24, 32 bits per pixel and maxima of 255, while `transformArray()`
   assumes red at shift 16 and blue at 0, so a format with red at 0 is
   drawn with red and blue swapped.
2. **Copy by rows, not by pixels,** in the other decoders' loops: Tight's
   copy, palette and gradient filters and `drawJpegBytes()`, and ZRLE
   (`ZrleDecoder.cpp:361`), which has the same `i % width`, `i / width`
   pattern. Raw and Hextile already read whole rows.
3. **Decode into the DIB** (Phase 4 step 4 of `async-networking.md`,
   reworked): the frame buffer's memory comes from a DIB section the GUI
   allocates, the local cursor is drawn at paint time instead of into
   the pixels, and `m_rectangleFb`, `FbUpdateNotifier` and copies 1 to 3
   above go. Only the blit remains.
4. **Find out what the paint calls cost** (WPA), before changing how
   updates are invalidated or painted. Painting at a matching depth, 1:1,
   is GDI's fast path; `StretchBlt` or a depth conversion is much slower.
   Beyond that is Direct2D or Direct3D: a separate project.

### Does removing `FbUpdateNotifier` cost performance?

No. Decoding has always been on the core (now network) thread; the
notifier runs in parallel only the copy into the DIB (0.4 s here) and
the `InvalidateRect` calls. Once the decoders write into the DIB, that
copy does not exist, so there is nothing left to parallelize. But the
removal should come after items 1 and 2 and together with 3, rather
than with the copy of step 4 as first planned, which would move the
notifier's copy onto the network thread. Decided with Steve
(2026-10-26): Phase 4 goes on with steps 5 to 7 first.
