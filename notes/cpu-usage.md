# Observations on CPU usage

Preliminary observations from the main author.  The following discussion needs uploaded profiling data backing it up.

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


