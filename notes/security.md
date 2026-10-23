# Security review notes

What kinds of security problems the inherited TightVNC code has, so that
later work knows what to look for. The individual fixes are described in
their commit messages: search the log for `[SECURITY]` and
`[PORTABILITY]`.

## Threat model and priority

The attacker is a **VNC server the viewer connects to**: everything read
from the connection is untrusted, including what is read before
authentication (the ServerInit desktop name, the authentication failure
reason). A second, weaker attacker is someone who can **place files where
the viewer runs from**, such as a Downloads folder.

Most problems found so far are in **file transfer**, and the rest need a
malicious or broken server. Neither matters much for connecting to one's
own servers, so security review is not a priority in itself. But when work
touches a piece of code with these patterns, fix them then.

## What to look for

### Lengths and counts from the server

The most common flavour. The protocol sends a 32-bit length or count, and
the code trusts it:

- **Allocating it before reading anything**: `std::vector<char>
  buffer(length)`, `new FileInfo[count]`. The server can ask for up to
  4 GiB, or far more for a count. Limit it, or check it against the data
  that must follow.
- **`length + 1` in 32-bit arithmetic** wraps to 0 for 0xFFFFFFFF. The
  buffer is then empty and `&buffer.front()` is null.
- **Two sizes for one buffer**: a compressed and an uncompressed size, of
  which only one describes the buffer actually filled. The code used the
  other one for reads and writes, which leaked heap memory into downloaded
  files. Use the buffer's own `size()`, and check that the sizes agree.
- **`read()` where `readFully()` was meant**: one read returns what the
  socket has, and the rest of the field is then parsed as the next message.

Places that still read lengths from the server: `ft-client-lib` (only
`FileTransferReplyBuffer` has been reviewed), the decoders in
`viewer-core` (TightVNC 2.8.90 hardened them; not reviewed here), and any
`readUInt32()` or `readUInt16()` whose result sizes a buffer or loop.

### Text used as a format string

`Exception` used to take its message as a printf format, and most callers
passed pre-formatted text, including text from the server. Two log calls
did the same. A `%s` in that text reads a pointer that was never passed.

- Format strings must be literals or other program text; anything else
  goes through `"%s"`.
- `StringStorage::format()`, `AnsiStringStorage::format()` and the
  `LogWriter` functions are annotated `_Printf_format_string_`, so a build
  with `/analyze` checks their arguments as it does printf's. Annotate any
  new printf-style function the same way.
- `/analyze` does not flag non-literal format strings. Search for calls
  whose first argument is not `_T("...")`.

### Buffer sizes in bytes versus characters

`sizeof(buffer)` for a `TCHAR` buffer is twice its length in characters.
`FormatMessage` takes a size in characters and was given it in three
places; many other Win32 functions (`GetWindowText`, `ToUnicodeEx` and so
on) take characters too. Also check
for off-by-one terminator writes: `buffer[count] = 0` after a function that
may fill all of `buffer`.

### Unchecked failure

Win32 calls whose failure was ignored, so the code went on to use an
output buffer that was never written (`FormatMessage` in
`SocketException`), or a count of -1 (`_vsctprintf`) that became a size of
0. Look for the return value being dropped, especially before the output is
used.

### Invariants assumed but not kept

The string classes assume their buffer always ends with a null character,
so `getString()` returns a terminated string. Some failure paths and one
setter broke that. The invariant is now documented on `StringStorage`, and
every function that changes the string must keep it, including when it
throws. Similar unstated assumptions are likely elsewhere: when code relies
on one, write it down and check the functions that change the state.

### Starting programs and loading by name

`CreateProcess(NULL, "explorer ...")` searched the viewer's own directory
for `explorer.exe` before the system directories. Look for
`CreateProcess`, `ShellExecute`, `LoadLibrary` and `WinExec` with names
that are not full paths. Prefer a shell API that starts nothing by name, or
a full path from `GetSystemDirectory()`.

### Undefined behaviour that MSVC tolerates

Not exploitable with MSVC today, but worth fixing when seen, as
`[PORTABILITY]`: `memcpy` on overlapping ranges, a `va_list` read twice,
`size_t` or 64-bit arguments for `%d`/`%ld` (`long` is 32 bits on Windows,
so `%ld` is also wrong on Win32 with MSVC), and `front()` on a vector that
may be empty (use `data()`).

### Empty rectangles

`Rect::isValid()` accepts a rectangle of zero width or height, and the
RFB protocol allows empty rectangles, so clipping can leave an empty
rectangle at the right or bottom edge of a buffer, where its first pixel
lies past the end of a row or of the buffer. Code that loops over a
rectangle must check `isEmpty()` before touching its first row or pixel;
`FrameBuffer::fillRect()` did not, and wrote past the frame buffer.
Coordinates that a message gives relative to an enclosing rectangle
(RRE subrectangles, Hextile tiles and subrectangles) should also be
checked against it, not only against the buffer.

## Open findings

Seen while fixing other things, not yet fixed:

- **`JpegDecompressor`**: `errorExit()` throws a C++ exception through
  libjpeg's C code. On MSVC that works and leaks nothing (libjpeg
  allocates only from pools that `jpeg_destroy()` frees), so it is not a
  security issue, but it is not portable and depends on `/EHsc`
  details; and `init()` sets the error handler too late. The plan is to
  move to the TurboJPEG API: see `image-decoding.md`.

## Tools

- **`/analyze`**: set `$env:_CL_ = '/analyze'` and rebuild. Use
  `/flp:logfile=...;verbosity=minimal` for the log, since the console
  logger wraps long lines. At the last run it reported nothing for format
  arguments. The remaining warnings have not been looked at; most are
  possible null dereferences (C6011) in `region/x11region.c`, plus a
  handful of others such as C6387 in `util/ResourceLoader.cpp`.
- **`/W4`**: set `$env:_CL_ = '/W4'`. Of its warnings, C4238 (address of a
  temporary) and C4701 (possibly uninitialized) are the ones worth reading
  for safety; C4100 (unused parameter) is noise.
- **Fuzzing and ASan**: `rfb-fuzz` in `ReleaseFuzz`, and the replay
  tests under `ReleaseAsan`; see `testing.md`. The first fuzzing run
  found the `fillRect()` overflow above.
- **Searching**: `readUInt32()` and `readUInt16()` for server lengths;
  `sizeof(` next to `TCHAR` buffers; `getString()` passed as a format
  string.
