# Plan: string encoding and string classes

A plan (2026-10-23) to reduce the string classes inherited from TightVNC
to one, `StringStorage`, and to decode the text that RFB carries
according to the protocol rather than the Windows ANSI code page.
Nothing here is implemented yet. It is meant to follow the protocol
tests in `testing.md`, so that the replay cases give a base to compare
against.

See also `portability.md` ("TCHAR"), which is the larger, separate
question of what `TCHAR` and `StringStorage` hold on Linux.

## Current state

`win32-lib/util` has four string classes:

- `StringStorage`: the `TCHAR` string used everywhere (about 650 uses).
- `AnsiStringStorage`: `char`, converted with `CP_ACP`.
- `Utf8StringStorage`: `char`, converted with `CP_UTF8`.
- `UnicodeStringStorage`: `WCHAR`. With `_UNICODE` always defined it is
  a copy of `StringStorage`; it only mattered for the old non-Unicode
  configurations, whose `#ifndef _UNICODE` branches are dead code.

The other three have few users:

| Class | Users |
|---|---|
| `AnsiStringStorage` | `RemoteViewerCore` (handshake, desktop name, capabilities, plain cut text), `RfbCutTextEventClientMessage::send`, `VncAuthentication`, `JpegDecompressor`, `tvnviewer/ConnectionData` (host, passwords) |
| `Utf8StringStorage` | `DataInputStream::readUTF8`, `DataOutputStream::writeUTF8` (file transfer, auth failure reason), `RemoteViewerCore::receiveServerCutTextUtf8`, `RfbCutTextEventClientMessage::sendUtf8` |
| `UnicodeStringStorage` | `ResourceLoader`, `WinCommandLineArgs`, `Utf8StringStorage` |

## What the protocol says

The rfbproto "String Encodings" section recommends UTF-8 for strings,
since implementations have historically used Latin-1 or Windows code
pages. The exception is the plain `ClientCutText`/`ServerCutText`
messages, which the spec still defines as ISO 8859-1 (Latin-1).
TigerVNC and RealVNC follow it: they send Latin-1 in plain cut text,
convert plain cut text they receive from Latin-1, and carry Unicode
through the Extended Clipboard pseudo-encoding. TightVNC servers offer
their own UTF-8 cut-text messages, which the viewer already uses when
the server announces them.

The ANSI code page is not right for any of these. It only happens to
work between a TightVNC viewer and server on Windows machines with the
same code page.

## Decisions

| Text | Today | Planned |
|---|---|---|
| Desktop name (ServerInit) | ACP | UTF-8 if valid, otherwise Latin-1 |
| 3.8 authentication failure reason | UTF-8 | UTF-8 if valid, otherwise Latin-1 |
| Plain `ServerCutText` | ACP | UTF-8 if valid, otherwise Latin-1 |
| Plain `ClientCutText` | ACP | Latin-1; characters outside it become `?` |
| TightVNC UTF-8 cut text | UTF-8 | unchanged |
| File transfer names and messages | UTF-8 | unchanged |
| Protocol version, capability signatures | ACP | Latin-1 (they are ASCII; Latin-1 cannot fail) |
| libjpeg error messages | ACP | Latin-1 (ASCII) |
| VNC password, as the DES key | ACP | UTF-8 |
| Host name in `ConnectionData` | ACP round trip | UTF-8 (see "Host names") |

- **"UTF-8 if valid, otherwise Latin-1"** reads old Latin-1 servers and
  modern UTF-8 ones alike: Latin-1 text with bytes above 0x7F is almost
  never valid UTF-8, and ASCII decodes the same either way. Received
  plain cut text uses it too, so servers that send UTF-8 against the
  spec still work.
- **No user setting** for the encoding. Old servers that are not
  internationalised send ASCII, which every choice above decodes.
- **Passwords.** The DES key is the first 8 bytes of the UTF-8
  password, as TigerVNC's `vncpasswd` stores it. Cutting at 8 bytes
  may split a code point; the key is bytes, so that is harmless. A
  TightVNC server on Windows with a non-ASCII password will no longer
  match, and saved passwords with non-ASCII characters change meaning;
  both are accepted. The obfuscated password in `.vnc` files is not
  real protection and is to be removed altogether later, in favour of
  private file permissions with disk encryption, or Windows Credential
  Manager (on the to-do list).
- **A Unicode clipboard with TigerVNC and RealVNC** needs the Extended
  Clipboard pseudo-encoding. That is a separate feature.

## Helper functions

The three classes are replaced by free functions in a new
`util/TextEncoding.h`; `StringStorage` stays the only string class.
Encoded text is typed as bytes, so it cannot be mixed up with the
`TCHAR` strings by accident:

```cpp
using ByteSpan = std::span<const std::byte>;

StringStorage fromUtf8(ByteSpan bytes);
StringStorage fromLatin1(ByteSpan bytes);
StringStorage fromRfbText(ByteSpan bytes);   // UTF-8 if valid, else Latin-1
std::vector<std::byte> toUtf8(const StringStorage &text);
std::vector<std::byte> toLatin1(const StringStorage &text);
```

- **Why `std::byte`.** Neither `char *` nor `unsigned char *` (`UINT8`,
  used for pixels and keys) converts implicitly to
  `std::span<const std::byte>`, so passing text or an unrelated buffer
  is a compile error at any warning level. Where a `char` buffer really
  holds undecoded text (the libjpeg message, the protocol version), the
  caller writes `std::as_bytes(std::span(...))`, which marks the place.
- **No terminators.** The span carries the length, so the read sites
  lose their `buffer(length + 1)` and `buffer[length] = '\0'`; they
  read into a `std::vector<std::byte>` or `std::array<std::byte, N>`
  and pass it directly. The returned bytes go to
  `writeFully(const void *, size_t)` as they are.
- **Contract, to be written as the functions' documentation:**
  - Pre: the span is valid for the duration of the call; it is not
    kept. Any bytes are accepted.
  - Post: decoding stops at the first NUL, since `StringStorage` is
    NUL-terminated (this is today's behaviour, by accident).
    `fromUtf8` replaces invalid sequences with U+FFFD, as
    `MultiByteToWideChar` does without `MB_ERR_INVALID_CHARS`.
    `fromRfbText` tries `MB_ERR_INVALID_CHARS` first and falls back to
    Latin-1. `toLatin1` maps characters above U+00FF, including both
    halves of a surrogate pair, to a single `?`.
  - Throws when the input exceeds `INT_MAX`, which the Win32
    conversions take as `int`; the protocol limits (`MAX_CUT_TEXT_LENGTH`,
    `MAX_DESKTOP_NAME_LENGTH`, `DataInputStream::MAX_UTF8_SIZE`) keep
    real inputs far below that.
- **Latin-1 needs no Win32 call**: each byte is its code point. Only the
  UTF-8 functions use `MultiByteToWideChar`/`WideCharToMultiByte`; on a
  Linux build with `TCHAR = char` holding UTF-8 they become a
  validating copy.
- **Fuzzing.** The functions are pure, so they make an easy extra
  libFuzzer target, including the round trip `fromUtf8(toUtf8(x))`.

### Wire details to keep

- `DataOutputStream::writeUTF8` sends the terminating NUL and counts it
  in the length. TightVNC servers expect that, so the replacement keeps
  it. `readUTF8` gets the same NUL from the server; stopping at the
  first NUL drops it.
- The plain cut-text messages carry no terminator; the UTF-8 one sent by
  `sendUtf8` does not either. Both stay as they are.

## Host names

`SocketAddressIPv4::resolve` already calls `GetAddrInfo` with `TCHAR`
strings, which is `GetAddrInfoW`. The only lossy step is
`ConnectionData`, which converts the host to ACP for `HostPath` and
back. `HostPath::validateHostNames` accepts only ASCII letters, digits,
`.`, `-` and `_`, so in practice nothing is lost today.

The planned change is the cheap one: `HostPath` stays `char`-based and
holds UTF-8, and `ConnectionData` converts with `toUtf8`/`fromUtf8`.
Its parsing splits at ASCII delimiters and uses no `<cctype>` calls, so
UTF-8 is safe in it. Internationalised host names stay unsupported
until the Asio rewrite, which resolves with `GetAddrInfoExW` and takes
the name as UTF-16; see "Host name resolution" in
`async-networking.md`.

## Logging

`RemoteViewerCore::sendCutTextEvent` logs the clipboard text at detail
level, and both `receiveServerCutText` functions log it at debug level,
so copied passwords can end up in log files. These lines should log the
length only. That is a `[SECURITY]` change of its own, and can go in at
any time.

## Order of work

Each step builds in all four configurations and runs the replay tests.

1. Add `TextEncoding.h`/`.cpp` with the functions above.
2. Move `DataInputStream::readUTF8`/`DataOutputStream::writeUTF8` and
   the UTF-8 cut-text functions to them; remove `Utf8StringStorage`.
3. Move the remaining `RemoteViewerCore`, `RfbCutTextEventClientMessage`,
   `VncAuthentication` and `JpegDecompressor` sites, with the decisions
   above; this is the commit that changes behaviour.
4. Move `ConnectionData` (host and passwords); remove
   `AnsiStringStorage`.
5. Replace `UnicodeStringStorage` in `ResourceLoader` and
   `WinCommandLineArgs` with `StringStorage`; remove it.
6. Remove the files from the projects and the tracked filters files.

The work is small: about ten source files besides the six removed.

### Tests to add

Replay cases (`testing.md`) for each decision, so the change in step 3
shows up as an intended difference in `.events`:

- desktop names in ASCII, UTF-8, Latin-1 and invalid UTF-8;
- plain `ServerCutText` in Latin-1 and in UTF-8, and the TightVNC UTF-8
  message;
- a 3.8 authentication failure with a non-ASCII reason;
- VNC authentication with a non-ASCII password, with the expected DES
  response in `.out`;
- an embedded NUL in each of these.

Status (2026-10-23): `rfb-replay` logs the desktop name
(`desktop-name`), and the generator sends each kind of text and expects
the decisions above (`rfbgen.rfb_text()`). Cases exist only where
today's viewer already decodes that way: an ASCII desktop name, ASCII
plain cut text, UTF-8 cut text through the TightVNC message, and a UTF-8
failure reason (`text-ascii-and-utf8`, `vnc-auth-failed-utf8-reason`).
The rest go in with step 3. Seen on the sandbox (ANSI code page 1252) in
a throwaway run: a UTF-8 desktop name "Grüße 日本" came out as
"GrÃ¼ÃŸe æ—¥æœ¬", while Latin-1 plain cut text "café £ ÿ" came out right,
only because those characters are the same in Windows-1252; bytes
0x80 to 0x9F, or another code page, would differ.
