# Other VNC clients: sockets, TLS and features

A survey (2026-10-21) of the other open-source C++ VNC viewers that run
on Windows, for comparison with SquirrelVNC's design and feature list.
TurboVNC is left out: it has retired its Windows-specific viewer, and
all platforms use its Java viewer.

Sources read, all shallow clones of the default branch:

| Project | Commit | Date |
|---|---|---|
| TigerVNC | `e49ad12` | 2026-10-20 |
| UltraVNC | `92bb19b` | 2026-10-18 |
| neatvnc (the server library under wayvnc) | `3d9bbbd` | 2026-10-04 |
| wayvnc | `3512b71` | 2026-10-04 |

## How they handle sockets

### TigerVNC: non-blocking, one owner, restart on short data

TigerVNC's viewer (FLTK, cross-platform) is close to the plan in
`async-networking.md`, minus the coroutines.

- **Main thread only.** The socket is registered with FLTK's event loop
  (`Fl::add_fd`). On readable data, `CConn::processNextMsg` calls
  `processMsg()` in a loop, re-queued by a 0 ms timer, until it runs out
  of data.
- **Writes are buffered**, and the socket is watched for writability
  while data remains (`FL_WRITE`). Nothing blocks on `send()`, and no
  other thread touches the socket.
- **The handshake is a hand-written state machine**:
  `RFBSTATE_PROTOCOL_VERSION`, `SECURITY_TYPES`, `SECURITY`,
  `SECURITY_RESULT`, `INITIALISATION`, `NORMAL` and so on, each step
  returning "not enough data yet".
- **Parsing restarts on short data**, as in noVNC: `rdr::InStream` has
  `setRestorePoint()`, `hasDataOrRestore()` and `gotoRestorePoint()`.
  There are about 70 such checks across the message reader, the
  decoders and the security handlers.
- **Hextile needs saved state per tile**, as predicted in
  `async-networking.md`: `HextileDecoder::readRect` keeps its position
  in a member (`readTile`) and restores per tile.
- **Reading and decoding are separate.** `readRect` on the main thread
  only copies a rectangle's raw bytes into memory; `decodeRect` runs on
  a `DecodeManager` pool, one thread per CPU, keeping the order of
  overlapping rectangles. **Worth borrowing** after the event loop is in.
- **TLS** is GnuTLS wrapped as stream layers (`TLSInStream`,
  `TLSOutStream`), which works because one thread owns the connection.
- **Prompts run nested event loops.** `getUserPasswd` shows a modal
  dialog from inside message processing, and `processNextMsg` asserts
  that it is not re-entered (`static bool recursing`): the re-entrancy
  hazard that coroutines avoid.
- No TCP keepalive.

TigerVNC shows that one owner with non-blocking reads works, and what it
costs without coroutines: about 70 manual restart checks, saved state
per Hextile tile, and a state machine for the handshake.

### UltraVNC: blocking, a thread per connection

The same model as TightVNC.

- **`ClientConnection` is an `omni_thread`** that reads with blocking
  `ReadExact`. `ClientConnection.cpp` is about 8,000 lines.
- **Writes come from the GUI thread under `m_writeMutex`**, with two
  mitigations that TightVNC lacks: `WriteExactQueue` batches small
  writes, and `WriteExact_timeout` waits for writability with
  `select()` and a timeout, so a stalled server cannot hang the GUI
  indefinitely.
- **Keepalive is an UltraVNC protocol extension**
  (`rfbEncodingEnableKeepAlive`, `rfbKeepAlive`), which works only with
  UltraVNC's server. No TCP keepalive.

### How UltraVNC does VeNCrypt with two threads

`vncviewer/ClientConnectionTLS.cpp` (835 lines) uses **Schannel**,
plugged into UltraVNC's existing interface for encryption plugins
("DSM" plugins):

- `TLSPlugin::TransformBuffer` encrypts (`EncryptMessage`). It is called
  from `WriteExact` under `m_writeMutex`, on whichever thread writes.
- `TLSPlugin::RestoreBuffer` decrypts (`DecryptMessage`). It is called
  from `ReadExact` on the reader thread, **without a lock**: the
  plugin's restore mutex is deliberately not taken for this kind of
  plugin (`omni_mutex_conditional_lock ... m_pPluginInterface ? false :
  true`).

So encryption and decryption run **concurrently on one Schannel
context**, from two threads. That works because Schannel tolerates one
`EncryptMessage` running alongside one `DecryptMessage`, much as .NET's
`SslStream` on Windows allows one read and one write at a time. Each
direction has its own keys and sequence numbers.

The gap is **renegotiation and other post-handshake messages**. When
`DecryptMessage` returns `SEC_I_RENEGOTIATE`, the reader thread calls
`InitializeSecurityContext` on the same context and sends the reply
token through `Write` (which takes the write mutex for the send only).
The handshake call is not excluded from a concurrent `EncryptMessage`,
and it shares the `TLSSession::outBuffers`/`outDesc` members that the
encrypting side also uses: a data race if a write happens meanwhile. In
practice this is rare with TLS 1.2, where servers seldom renegotiate.
With TLS 1.3 (Schannel clients on Windows 11 or later), Schannel reports
post-handshake messages such as session tickets the same way, so it
happens on most connections, though usually right after the handshake,
before the GUI sends input.

Certificates: UltraVNC validates manually
(`SCH_CRED_MANUAL_CRED_VALIDATION`, `SECPKG_ATTR_REMOTE_CERT_CONTEXT`,
`CertGetCertificateChain`, `CertVerifyCertificateChainPolicy` with
`CERT_CHAIN_POLICY_SSL`), and on failure asks the user and pins the
certificate's thumbprint in a `[thumbprint]` section of its `.ini` file:
the trust-on-first-use behaviour planned for SquirrelVNC.

**Lesson for SquirrelVNC:** TLS with two threads can be made to work
with Schannel, as long as the handshake path is excluded from
encryption. A single owner of the connection removes the problem
altogether, and is still the plan.

## Security types: what wayvnc offers

From neatvnc's `init_security_types` (`src/server.c`), with wayvnc's
`enable_auth=true`:

| Type | When offered |
|---|---|
| RSA-AES-256 (129), RSA-AES (5) | Always, listed first. Without `rsa_private_key_file`, neatvnc generates a new RSA key at each start. |
| VeNCrypt (19) | Only when a certificate and private key are configured, and then only the **X509Plain** subtype (`src/auth/vencrypt.c`). |
| Apple DH (30) | Only with `relax_encryption`. |
| VNC authentication (2) | Only with `relax_encryption` and `allow_broken_crypto`. |

So with authentication on, SquirrelVNC (None, VNC authentication and
Tight today) cannot connect to wayvnc unless both "relaxed" options are
set.

Elsewhere, VeNCrypt is the older and broader standard (TigerVNC's
server, QEMU, x11vnc, wayvnc). RSA-AES comes from RealVNC ("RA2") and
has been adopted by TigerVNC, UltraVNC and neatvnc. (Servers other than
neatvnc are from memory, not checked in their source.)

### Decision: VeNCrypt X509 over Schannel; no RSA-AES

**RSA-AES will not be implemented.** It is a custom protocol, not TLS:
each side sends its RSA public key, then a random value encrypted with
the other's key using PKCS#1 v1.5 (neatvnc calls nettle's
`rsa_encrypt`/`rsa_decrypt`); session keys are derived from the two
randoms with SHA-1 or SHA-256; messages are length-prefixed chunks
encrypted with AES-EAX. It has no forward secrecy, and the server is
authenticated only by trust on first use of its RSA key, which neatvnc
regenerates at each start by default. On Windows, CNG has no EAX mode
(nor does OpenSSL), so it would mean building EAX from CTR and CMAC by
hand. Anyone who cares about the security of their server can set up a
self-signed certificate instead, which SquirrelVNC will accept on first
connection and pin, like SSH.

**TLS with Schannel:**

- **No anonymous TLS.** Schannel has no anonymous cipher suites, so
  VeNCrypt's `TLSNone`, `TLSVnc` and `TLSPlain` (anonymous
  Diffie-Hellman, open to man-in-the-middle anyway) cannot be done; only
  the X509 subtypes. neatvnc offers only X509Plain, so nothing is lost.
- **Self-signed certificates are the normal case** with wayvnc, so
  validate manually, as UltraVNC does: `SCH_CRED_MANUAL_CRED_VALIDATION`,
  fetch the server certificate, try the normal chain and host-name check,
  and if that fails, offer to trust and pin the certificate's SHA-256
  fingerprint in the `.ini` file.
- **TLS version:** Schannel supports TLS 1.3 as a client only on
  Windows 11 and Server 2022 or later. On Windows 10 it is TLS 1.2 with
  ECDHE (forward secrecy), which neatvnc supports through GnuTLS.
- **The API suits the design.** SSPI works on buffers: an
  `InitializeSecurityContext` loop for the handshake, then
  `EncryptMessage`/`DecryptMessage`, with "incomplete message" and
  "extra data" results: the bytes-in, bytes-out shape of the sans-I/O
  core. Expect a few hundred lines, as in UltraVNC.
- **X509Plain sends a username and password** inside the tunnel, to be
  kept in Windows Credential Manager keyed by `user@host`.

## Features that SquirrelVNC lacks

Leaving out H.264, Continuous Updates and TLS, which are already in
`async-networking.md`.

### Protocol extensions

These work with any server that implements them. The last column is
checked against neatvnc's `include/rfb-proto.h`.

| Feature | Tiger | Ultra | neatvnc | Notes |
|---|---|---|---|---|
| **QEMU Extended Key Event** | ✓ | | ✓ | Sends scan codes as well as keysyms, avoiding keyboard-layout mismatches and dead-key problems; relevant given the complexity of `RfbKeySym`. |
| **LED state** | ✓ | | ✓ (QEMU and VMware) | Keeps Caps Lock and Num Lock in sync. |
| **Extended Clipboard** | ✓ | ✓ | ✓ | The common standard for UTF-8, large text and several formats. SquirrelVNC has only TightVNC's own UTF-8 clipboard extension. |
| ExtendedDesktopSize, remote resize | ✓ | ✓ | ✓ | Desktop resizing in `async-networking.md`. |
| Desktop name changes | ✓ | ✓ | ✓ | Window title updates during a session. |
| Cursor with alpha (VMware) | ✓ | | | Smooth, anti-aliased cursors. |
| Extended mouse buttons | ✓ | | ✓ | Back and forward buttons. |
| Audio (QEMU audio) | ✓ | | ✓ | |
| RSA-AES | ✓ | ✓ | ✓ | Not to be implemented; see above. |

UltraVNC also has extensions that work only with its own server: its
keepalive message, monitor selection (`rfbSetSW`, `rfbMonitorInfo`),
a cache encoding, zstd and xz encodings, GII touch input, and text chat.

### Viewer features

- **Auto-reconnect.** TigerVNC has `ReconnectOnError`; UltraVNC has
  `m_autoReconnect` with a retry counter. Fits the always-on monitor
  use, together with keepalive.
- **Touch.** TigerVNC maps gestures to mouse and wheel events: one-,
  two- and three-finger taps, drag, two-finger drag to scroll, pinch to
  zoom, and long press (`GestureHandler`, `Win32TouchHandler`).
- **Multi-monitor.** TigerVNC goes full screen across all monitors or a
  chosen set (`FullScreenAllMonitors`, selected monitors), using the
  standard ExtendedDesktopSize screen layout, so it works with any
  server supporting that. UltraVNC's monitor selection and "extend
  display" need its own server.
- **Direct3D rendering** (UltraVNC): Direct3D 9. A modern equivalent
  would be Direct2D or Direct3D 11 with the flip model, which is also
  where hardware H.264 decoding would deliver its frames.
- **Pointer-rate limiting**: TigerVNC's `PointerEventInterval` and
  UltraVNC's `throttleMouse`; the pointer coalescing in
  `async-networking.md`.
- **Automatic encoding and quality choice** by measured bandwidth
  (TigerVNC `AutoSelect`).
- **Middle-button emulation** (both).
- **Prior art for planned features**, both in UltraVNC:
  - passwords in Credential Manager (`CredentialManager.cpp`), keyed by
    a prefix (`vnc`, `mslogon`) plus host and port;
  - a portable mode: an empty `ultravnc.portable` file next to the exe
    puts its `.ini` in the application folder; otherwise
    `%LOCALAPPDATA%`, then `C:\ProgramData` (`PORTABLE_MODE.md`).
- **Smaller items**: SSH tunnelling with `via` (Tiger), proxy and
  repeater (Ultra), IPv6 (both), a dot when the remote cursor is hidden
  (Tiger), snapshots to an image file, a Japanese keyboard mode and an
  idle timeout (Ultra).

### Priorities for SquirrelVNC

1. **Auto-reconnect**, with keepalive: small, and fits the monitor use.
2. **QEMU Extended Key Event**: fixes a class of keyboard bugs.
3. **Extended Clipboard**: the standard way to do Unicode clipboards.

Touch, multi-monitor and audio are marketing features, not priorities.
If they are done, TigerVNC's designs are the ones to follow, since they
stay within the standard protocol.
