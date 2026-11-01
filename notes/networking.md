# Networking: IPv6 and host-name lookup

**Historical once replaced.** The layers described here are the
original design from TightVNC. The asynchronous rewrite
(`async-networking.md`) replaces them with Asio, which brings IPv6 and
cancellable connect, and with its own cancellable lookup on
`GetAddrInfoExW()` ("Host name resolution" there); the host-string
syntax below still applies.

Notes for later work on the connection code. Host names are now looked
up with `GetAddrInfo()`, asking for IPv4 addresses only; everything
below is still to do.

*Forward connections support IPv6 since 2026-10-31 (see "Status of
IPv6", at the end). Listening mode stays IPv4-only, by decision: no
open-source VNC server is known to make reverse connections.*

## IPv6 is not supported anywhere

Four layers are IPv4-only, so changing the lookup alone would not help:

1. **Host string parsing** (`rfb/HostPath.cpp`). The first `:` after the
   host starts the display or port (TightVNC's `host:display` and
   `host::port`), and host names must consist of letters, digits, `.`,
   `-` and `_`. An IPv6 literal is rejected before any network code runs,
   with or without brackets.
2. **Lookup** (~~`SocketAddressIPv4::resolve()`: `GetAddrInfo()`~~
   *`asyncResolve()`, `GetAddrInfoExW()`, since Phase 4 step 2a of
   `async-networking.md`*) with `ai_family = AF_INET`.
3. **Sockets** (~~`SocketIPv4`~~ *Asio's, since Phase 4; `SocketIPv4` was
   deleted in step 7*): ~~always `AF_INET`. `connect()` and `bind()`
   take a `SocketAddressIPv4`, which wraps a `sockaddr_in`.~~ *an
   accepted socket is IPv4 because the acceptor binds `0.0.0.0`; a
   connected one because the lookup returns IPv4 addresses only.*
4. **Listening mode** (~~`TcpServer`~~ *`TcpAcceptor`, since Phase 4
   step 6 of `async-networking.md`*, used by `ConnectionListener` on port
   5500): binds `0.0.0.0` only.

~~`SocketIPv4` is also the type passed around in `TcpConnection`,
`RemoteViewerCore`, `ViewerInstance` and `ConnectionListener`, so those
signatures change too.~~ *Since Phase 4 of `async-networking.md`, they
pass `asio::ip::tcp::socket`, which carries its protocol.*

### Plan

- **Lookup**: `GetAddrInfo()` with `AF_UNSPEC`, trying each address in
  the order returned until one connects, so a host with both A and AAAA
  records falls back to IPv4.
- **Sockets**: create each with the address's own family; hold addresses
  as `sockaddr_storage`. Renaming `SocketIPv4` can be a separate step.
  *Asio's sockets already take the family from the endpoint.*
- **Listening**: one IPv6 socket on `::` with `IPV6_V6ONLY` off, which
  accepts IPv4 and IPv6 callers.
- **Parsing**: accept bracketed literals: `[fd00::5]`, `[fd00::5]:1`,
  `[fd00::5]::5901`. Brackets must be required: bare literals clash with
  TightVNC's syntax (`::1` already means "localhost, port 1", and
  `fe80::1` would parse as host `fe80`, display 1). Link-local addresses
  need a zone, so allow `%` inside the brackets: `[fe80::1%12]`.
- **Display**: show IPv6 addresses in brackets in the window title and
  the log.

Suggested order: lookup and family-agnostic sockets, then the parser,
then listening mode.

### Testing

The LAN is IPv6-enabled. Needed:

- A VNC server listening on IPv6 (on Linux, `ss -ltn` shows
  `[::]:5900`); many servers bind IPv4 only unless told otherwise.
- A global or unique-local (`fd…`) address is simplest; link-local needs
  the `%interface` suffix.
- A name with an AAAA record (DNS, the laptop's hosts file, or mDNS) to
  test lookup, and one with both A and AAAA, with IPv6 to it blocked, to
  test the fallback to IPv4.
- Listening mode: the server connects back to the laptop's IPv6 address.

## Lookup and connect cannot be cancelled

The lookup is synchronous but runs on the connection's own thread: each
viewer window's `RemoteViewerCore` thread calls `connectToHost()`, then
`TcpConnection::connect()`, then a blocking `GetAddrInfo()` and a
blocking `connect()`. So a slow lookup does not freeze the UI.

But `RemoteViewerCore::stop()` interrupts the thread only by closing its
socket stream, which does not exist until the connection is made. During
the lookup or the `connect()`, `stop()` does nothing, and whoever waits
for the thread (`Thread::wait()`, `INFINITE`) blocks until the lookup or
connect finishes or times out: for example at program exit
(`ViewerCollector::destroyAllInstances()`), or when a `ViewerInstance`
is destroyed.

Windows has what is needed to fix this, unlike the portable sockets API:

- `GetAddrInfoExW()` with an `OVERLAPPED` and a completion routine or
  event (Windows 8 and later), and `GetAddrInfoExCancel()` to abort it.
- A non-blocking `connect()` waited on with something `stop()` can
  signal, or closing the socket from the stopping thread.

Best done with the IPv6 work, which reworks the same connect path.

## Status of IPv6 (2026-10-31)

Done, for forward connections, in the order the plan suggested:

- **Lookup**: `asyncResolve()` asks `GetAddrInfoExW()` for `AF_UNSPEC`,
  and converts `AF_INET6` results with their scope (zone).
  `asio::async_connect()` tries the addresses in the order Windows sorts
  them (RFC 6724), so a host with A and AAAA records falls back to IPv4.
  `AI_ADDRCONFIG` is not passed: on Windows it does not count loopback
  addresses as configured, so it would make `::1` fail on a computer
  with no other IPv6 address. (Windows' DNS client is said to skip AAAA
  queries by itself on such a computer; not verified.)
- **Sockets**: nothing to do; Asio's socket opens with the family of
  each endpoint it tries.
- **Parsing** (`HostPath`): bracketed literals as planned, validated with
  Asio's `make_address_v6()`. The zone must be a number (Windows'
  interface index); names like `%eth0` are refused, as Windows does not
  use them. `getVncHost()` returns the address without brackets, which
  is what `GetAddrInfoExW()` takes. An SSH host cannot be IPv6 (the SSH
  tunnelling inherited from TightVNC is not implemented anyway).
  `HostPath::joinHostAndPort()` brackets an IPv6 host when a host and
  port come separately: from a `.vnc` file, which stores the bare
  address, and from `-host` with `-port`.
- **Display**: the connection log brackets an IPv6 host and names the
  address connected to. The window title shows the path as typed,
  brackets included.
- **Listening**: not done, by decision (IPv4 only, `0.0.0.0`).

Tests: `HostPathTests` (bracketed forms, malformed ones, bare literals
keeping TightVNC's meaning, `joinHostAndPort()`), `HostResolverTests`
(`::1`, and `fe80::1%5` keeping its scope), and every replay case run
over `::1` (`rfb-replay --tcp6`). Not tested automatically: a real
server over IPv6, a name with AAAA records, or the fallback to IPv4.

Left for later:

- **Slow fallback.** The addresses are tried one after another, each
  until it fails. Where IPv6 is broken silently (packets dropped rather
  than refused), the fallback to IPv4 waits for the connect timeout,
  about 21 seconds on Windows. Happy Eyeballs (RFC 8305), staggered
  attempts in parallel, would fix it; worth doing if it is met in
  practice.
- **A bare IPv6 address is silently misread**: `fd00::5` connects to the
  host `fd00`, port 5, as TightVNC's syntax says. A warning, or
  rejecting a "host" that is all hexadecimal followed by `::`, might
  save confusion, but would break the rare host name of that form.
