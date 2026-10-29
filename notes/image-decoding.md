# Plan: JPEG decoding with libjpeg-turbo's TurboJPEG API

A plan (2026-10-23) for replacing the classic libjpeg API in
`core-lib/viewer-core/JpegDecompressor.cpp`, which decodes the JPEG
rectangles of the Tight encoding, with libjpeg-turbo's TurboJPEG API.
Nothing here is implemented yet.

**Priority: low, after Tight replay tests.** It is a solid improvement,
but nothing is broken today, and the work queue is long. Before it is
done, `rfb-replay` needs replay cases for the Tight encoding, JPEG
included (see "Prerequisite" below), so that the rewrite can be checked
without running the viewer by hand.

The facts about libjpeg below are from memory of its documentation and
source, not re-checked for this note; check them when doing the work.

## The code today

- `JpegDecompressor::decompress()` uses the classic API:
  `jpeg_mem_src()`, `jpeg_read_header()`, `jpeg_start_decompress()`,
  `jpeg_read_scanlines()` row by row, `jpeg_finish_decompress()`. It
  asks for RGB, 3 bytes per pixel, and `TightDecoder` converts that
  buffer into the frame buffer's pixel format afterwards.
- On a fatal error, libjpeg calls the `error_exit` handler, which must
  not return. `JpegDecompressor::errorExit()` calls `jpeg_destroy()` and
  throws a C++ `Exception` through libjpeg's C frames;
  `decompress()` catches it, destroys and re-creates the decompressor,
  and rethrows.

## When libjpeg calls `error_exit`

Only for fatal errors; warnings go to `emit_message`, and decoding goes
on.

- Malformed or unsupported data in the headers and markers: a bad
  Huffman or quantization table, impossible dimensions or sampling
  factors, an image too large for the library's limits.
- Misuse of the API: functions called in the wrong order
  (`JERR_BAD_STATE`), bad parameters.
- Out of memory, reported by the memory manager.

Running out of input is not fatal (the memory source inserts a fake
end-of-image marker), and nor, at least in libjpeg-turbo, is corrupt
entropy-coded data. A truncated or damaged image body usually decodes to
garbage rather than failing.

## Is recovery from `error_exit` supported?

Yes, by design. The IJG's `libjpeg.txt` says `error_exit` must not
return: it either exits the program or `longjmp`s back to a point the
application chose, as `example.c` shows. Afterwards `jpeg_abort()` makes
the object reusable, and `jpeg_destroy()` frees it.

That is safe because the library allocates nothing except through its
memory manager. Memory comes in pools (and temporary backing-store
files, if any, are tracked the same way); `jpeg_abort()` frees the
per-image pool and `jpeg_destroy()` everything. libjpeg-turbo keeps
that design, and its default memory manager uses no temporary files.
TurboJPEG itself works this way: each instance has its own `jmp_buf`,
and its error handler `longjmp`s back within libjpeg-turbo's own C code.

So throwing a C++ exception from `error_exit` is the same jump as the
documented `longjmp`, and on MSVC both unwind the same way (SEH). The C
frames skipped hold nothing but pool memory, and `errorExit()` destroys
the decompressor before throwing, so nothing leaks. **It is not a
security issue.** What is wrong is portability, and reliance on MSVC's
exception handling:

1. **Throwing through C frames is MSVC-specific.** On Linux, a C++
   exception crosses C code only if that code was compiled with unwind
   tables (`-fexceptions`), which distribution builds of libjpeg usually
   are not; the program would likely terminate.
2. **`/EHsc`.** The `c` tells the compiler that `extern "C"` functions
   never throw, and every `jpeg_*` function is `extern "C"`. The `try`
   in `decompress()` survives, as it also contains C++ `throw`s, but
   whether exception handling stays correct at the call sites into
   libjpeg is not certain; strictly the file needs `/EHs`. Untested
   either way: no test produces a JPEG error yet.
3. **`init()` sets `cinfo.err` after `jpeg_create_decompress()`.**
   libjpeg expects the error handler first; if creation itself fails
   (in practice only when out of memory), it calls an uninitialised
   handler. A two-line reordering, which can be done on its own.

Do not replace the exception with `setjmp`/`longjmp` back into C++:
`longjmp` on MSVC also unwinds with SEH, so it is no more portable, and
on other compilers it skips C++ destructors.

## What the TurboJPEG API gives

Its functions (`tj3Init()`, `tj3DecompressHeader()`, `tj3Decompress8()`,
`tj3Destroy()`) return error codes, with `tj3GetErrorStr()` for the
message; the `longjmp` stays inside libjpeg-turbo's C code. The vcpkg
build already installs `turbojpeg.lib` and `turbojpeg.h` next to
`jpeg.lib`. Besides removing points 1 to 3 above:

1. **Decoding straight into the frame buffer's layout.**
   `tj3Decompress8()` takes an output pixel format and a row pitch. For
   the usual 32-bit format, B, G, R, X in memory (`TJPF_BGRX`), it can
   write directly into the ~~rectangle buffer~~ frame buffer, saving a full copy and a
   per-pixel conversion in `TightDecoder`. Other formats keep the
   conversion. For sessions heavy in Tight JPEG, probably the largest
   practical gain. *Done 2026-10-29 with the classic API's
   `JCS_EXT_BGRA` and `JCS_EXT_RGBA` (`cpu-usage.md`, "What to do" 1);
   the move to TurboJPEG is still open.*
2. **Validating before decoding.** `tj3DecompressHeader()` gives the
   width, height and subsampling, so they can be checked against the
   rectangle before any image buffers are set up.
3. **Limits on hostile input.** A scan limit (`TJPARAM_SCANLIMIT`,
   formerly `TJFLAG_LIMITSCANS`) stops progressive JPEGs with huge
   numbers of scans, a known way to burn CPU. libjpeg-turbo 3.x also
   caps memory use (`TJPARAM_MAXMEMORY`), which ties in with the
   deferred question of decoders allocating too much
   (`testing.md`, "Later"). `TJPARAM_STOPONWARNING` turns "corrupt data"
   warnings into errors instead of decoding garbage.
4. **Clearer errors.** Each handle keeps its own error message, which is
   safe with several connections, and `tj3GetErrorCode()` tells warnings
   from fatal errors.
5. **Simpler, portable code.** One handle per decoder serves every
   rectangle, and decoding is one call instead of the header, start,
   scanlines, finish sequence. The same code works on Linux, where
   `libturbojpeg` is a standard package.
6. **Speed options,** if ever wanted: `TJPARAM_FASTDCT` and
   `TJPARAM_FASTUPSAMPLE` trade a little quality for speed.

The cost is depending on libjpeg-turbo rather than any libjpeg, which
we already do through vcpkg, as do the Linux distributions.

Not yet checked: which of these parameters the vcpkg version (3.2.0)
has, and whether vcpkg's automatic linking already picks up
`turbojpeg.lib`.

## Prerequisite: Tight replay cases

The rewrite should be checked against replay cases first, so they come
first. The generator (`tests/gen/rfbgen.py`) needs the Tight encoding:

- the control byte, with its stream-reset bits and stream ids;
- fill, with the 3-byte pixels used for 32-bit, depth-24 formats;
- basic compression with the copy, palette (2 colours packed as bits,
  and more) and gradient filters, sent uncompressed below 12 bytes and
  otherwise through one `zlib.compressobj` per stream, flushed with
  `Z_SYNC_FLUSH`, which keeps its state across rectangles as the
  viewer's inflaters do;
- JPEG, from a JPEG file encoded once and checked in, so that the
  generator needs no JPEG encoder. Its expected image is what the
  viewer's decoder produces, which may differ slightly from another
  decoder's; take it from a first run of `rfb-replay`, reviewed by eye,
  and note that in the case.

Cases for the errors the viewer must reject (an unknown compression
type, a JPEG whose size differs from the rectangle's) belong with them.

Status: done (`rfbgen.TightRect`, cases `tight-*`), including the
unknown compression type and data that inflates short. The JPEG cases
use `tests/gen/jpeg/quadrants-16x16.jpg`, made by
`make-quadrants-jpeg.c` beside it, and the decoded image
`quadrants-16x16.ppm`, taken from rfb-replay and reviewed. A JPEG the
viewer cannot decode is not an error: `processJpeg()` logs it and the
rectangle keeps its old contents (`tight-jpeg-wrong-size`).

~~Strictly, the rectangle gets the rectangle buffer's contents
(`m_rectangleFb`), which are the frame buffer's except where a CopyRect
has changed the area since: `CopyRectDecoder::copy()` writes to the
frame buffer only. So after a CopyRect, an undecodable JPEG over the
same area shows what was there before the copy. Harmless, but worth
fixing in the rewrite: draw nothing at all when the JPEG fails.~~
Fixed (2026-10-29): the decoders draw into the frame buffer itself, and
the rectangle buffer is gone, so an undecodable JPEG draws nothing.

## The rewrite, when it comes

1. Tight replay cases pass on the current code. Done.
2. Rewrite `JpegDecompressor` on the TurboJPEG API, keeping its
   interface; then, separately, let `TightDecoder` decode into the frame
   buffer's layout where it can.
3. The same cases pass, and the JPEG ones give the same image (the
   same libjpeg-turbo decoder underneath, so they should).
4. Remove the `JpegDecompressor` entry from the open findings in
   `security.md`.
