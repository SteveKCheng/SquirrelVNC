# Networking: IPv6 and host-name lookup

**Historical once replaced.** The layers described here are the
original design from TightVNC. The asynchronous rewrite
(`async-networking.md`) replaces them with Asio, which brings IPv6 and
cancellable lookup and connect; the host-string syntax below still
applies.

Notes for later work on the connection code. Host names are now looked
up with `GetAddrInfo()`, asking for IPv4 addresses only; everything
below is still to do.

## IPv6 is not supported anywhere

Four layers are IPv4-only, so changing the lookup alone would not help:

1. **Host string parsing** (`rfb/HostPath.cpp`). The first `:` after the
   host starts the display or port (TightVNC's `host:display` and
   `host::port`), and host names must consist of letters, digits, `.`,
   `-` and `_`. An IPv6 literal is rejected before any network code runs,
   with or without brackets.
2. **Lookup** (`SocketAddressIPv4::resolve()`): `GetAddrInfo()` with
   `ai_family = AF_INET`.
3. **Sockets** (`SocketIPv4`): always `AF_INET`. `connect()` and `bind()`
   take a `SocketAddressIPv4`, which wraps a `sockaddr_in`.
4. **Listening mode** (`TcpServer`, used by `ConnectionListener` on port
   5500): binds `0.0.0.0` only.

`SocketIPv4` is also the type passed around in `TcpConnection`,
`RemoteViewerCore`, `ViewerInstance` and `ConnectionListener`, so those
signatures change too.

### Plan

- **Lookup**: `GetAddrInfo()` with `AF_UNSPEC`, trying each address in
  the order returned until one connects, so a host with both A and AAAA
  records falls back to IPv4.
- **Sockets**: create each with the address's own family; hold addresses
  as `sockaddr_storage`. Renaming `SocketIPv4` can be a separate step.
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
