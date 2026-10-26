# How the protocol core is suspended and resumed

The mechanics behind Phases 3 and 4 of `async-networking.md`: how the
protocol code, a chain of coroutines (`Task<T>`, `util/Task.h`), stops
when it runs out of bytes, and who starts it again. Written 2026-10-26,
from a discussion with Steve, before Phase 3 was implemented.

## Three ways to resume a parser

Say the decoder, in a coroutine A, awaits a read, and the bytes have
not arrived yet.

- **Direct, as in .NET.** The read is itself an asynchronous operation
  B on the socket. When B completes, the socket's completion resumes A
  at its `await`. B (and the I/O machinery under it) holds A's
  continuation: it is a shared owner of A's frame. That suits eager,
  garbage-collected tasks; in C++ or Rust it brings lifetime and
  ownership problems, which can be solved, but at the cost of the
  model's simplicity.
- **Polled from the top, as in Rust.** A future stores no continuation.
  The I/O only wakes the executor, which polls the root future again;
  each `poll` re-enters the chain through every `.await`, down to the
  one that was pending, which now finds its data. Resuming costs time
  in proportion to the depth of the chain, but nothing holds a pointer
  into the chain.
- **Here: the leaf is resumed, by a single owner.** The read does no I/O.
  It records what it is waiting for, keeps the handle of the coroutine
  that awaits it (A), and suspends. The *driver*, which owns the
  connection, does the I/O, gives the bytes to the reader, and resumes
  A's handle. Resumption goes straight to the leaf, in constant time,
  as in .NET; but the handle is held by the reader, which the driver
  owns, and only the driver resumes it, as in Rust.

## The pieces

- **The chain of `Task`s**: `RemoteViewerCore::run()` at the root, down
  through the message loop and a decoder to the coroutine that reads.
  Each frame owns the `Task` of the child it awaits, so the chain is
  owned from the top. Only one chain runs per connection.
- **The reader** (`AwaitableReader`): it does no I/O. It holds a little
  of the connection's state:
  - the bytes received and not yet consumed;
  - the read that is pending, if any: how many bytes, where to copy
    them, how many have been copied so far;
  - what the chain needs before it can go on: more bytes, or the answer
    to a request such as the password;
  - an error to deliver instead, when the connection has failed or is
    being stopped;
  - the handle of the suspended coroutine: the one waiter.
- **The driver**: it owns the connection (socket, reader and root
  `Task`), and is the only code that calls `resume()`. In Phase 3 it is a
  loop on the core thread with blocking reads; in Phase 4 it is an
  `asio::awaitable` on the network thread.

## Suspending

`co_await reader.readUInt16()` makes an awaiter object. Awaiters live in
the frame of the coroutine that awaits them for as long as it is
suspended, so the reader may keep a pointer to it.

1. `await_ready()` is true if the buffer holds enough bytes; the read
   then completes at once, without suspending (the common case).
2. Otherwise `await_suspend(handle)` records the pending read and
   `handle` (A's frame) in the reader, and returns
   `std::noop_coroutine()`: suspend A, and resume nothing in its place.
3. Control returns to whoever last called `resume()`. Every link of the
   chain was entered by symmetric transfer (a `Task`'s awaiter returns
   the child's handle, and a finished `Task` returns its parent's), so
   no nested `resume()` calls are on the stack: the one that returns is
   the driver's.

So the leaf "tells" the driver by state, not by a call: the driver's
`resume()` returns, the root `Task` is not done, and the reader says
what is needed.

The stored handle is A's, the innermost frame, not the root's. That is
simply how C++ coroutines work, with or without frame elision: the
frames above A are themselves suspended in their `co_await` on their
child's `Task`, and resume in turn as each child finishes.

## Resuming

The driver gives the reader what it needs (`append()` bytes, answer a
request, or `fail()` with an exception), then calls `resumeWaiter()`:

- For a pending read, the reader copies what has arrived into the
  read's destination. Only once the whole read is satisfied does it
  resume A; otherwise A stays suspended and the driver reads more. A
  large `readFully` (a 4 MB Raw rectangle) is therefore not retried, nor
  resumed many times: it completes once, when its last byte arrives.
- A resumes inside its `co_await`: `await_resume()` returns the value,
  or throws the exception the driver recorded.
- A runs to its next suspension, or finishes; a finished `Task` resumes
  its parent by symmetric transfer, and so on up. When the root
  finishes, its continuation is `std::noop_coroutine()`, so control
  returns to the driver's `resumeWaiter()` call once more.

Every place where the parser can stop is a `co_await` on the reader,
and each read completes exactly once. Feeding a stream one byte at a
time exercises all of them (the split tests of Phase 3).

## Requests

Waits for something other than bytes go the same way. The password, in
VNC authentication, is awaited from the reader; the reader records the
request and suspends; the driver serves it and resumes the waiter with
the answer. In Phase 3 the driver serves it with the blocking wait of
Phase 1 step 2 (`ViewerWindow::askPassword()`); in Phase 4, with an
operation that the GUI completes. Later, the certificate prompt and
"start TLS now" (VeNCrypt) are requests too. The protocol code awaits
them in straight-line code, and the operations are visible in the
driver.

## Ownership and teardown

- The driver owns the root `Task`, and through it every frame; the
  reader holds only a borrowed handle of the one suspended leaf.
- The driver never destroys the root while the chain is suspended. To
  stop, it records an error in the reader (`fail()`) and resumes the
  leaf: `await_resume()` throws, and the chain unwinds by itself, its
  destructors and `catch` handlers running as usual, up to the root,
  which finishes with the exception. This is the cooperative unwinding
  decided in `async-networking.md` ("Corrections and decisions").
- Exactly one waiter at a time: one chain per connection, and a
  coroutine awaits one thing at a time.

## The driver

In Phase 3, on the core thread:

```cpp
Task<> root = run();           // lazy: nothing has run yet
root.start();                  // returns at the first suspension
while (!root.isDone()) {
  switch (reader.need()) {
  case Need::Bytes:
    reader.append(input.read(...));     // blocking; on failure, reader.fail(e)
    break;
  case Need::Request:
    reader.serveRequest();              // the password: blocking, for now
    break;
  }
  reader.resumeWaiter();       // returns at the next suspension, or at the end
}
root.result();                 // returns, or rethrows what left run()
```

In Phase 4 the same loop is an `asio::awaitable`: the bytes come from
`co_await socket.async_read_some(...)`, and the request is served with a
`co_await` on an operation that the GUI completes. The protocol code
does not change.

## Compared with Rust

Both keep the I/O from resuming the parser: a single owner does,
the driver here and the executor in Rust. Rust re-enters the chain from
the top on every poll and stores no continuation; C++ stores the leaf's
handle, resumes it directly, and climbs back up by symmetric transfer.
