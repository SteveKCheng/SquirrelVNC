#ifndef BUFFEREDINPUTSTREAM_H
#define BUFFEREDINPUTSTREAM_H

#include "DataInputStream.h"
#include <vector>

class BufferedInputStream final : public InputStream
{
public:

  BufferedInputStream(InputStream *input);

  ~BufferedInputStream() override;

  //
  // Reads up to len bytes into buffer: those buffered, if any, without
  // reading the underlying stream; otherwise it reads the underlying
  // stream once, which may block, buffers what it gets, and returns up to
  // len bytes of it.
  //
  // Pre-conditions: buffer holds len bytes.
  // Post-conditions: returns the number of bytes copied, at least 1 if len
  // is not 0.
  // Throws what the underlying stream throws.
  //
  size_t read(void *buffer, size_t len) override;

  size_t available() override;

protected:

  const size_t MAX_BUFFER_SIZE;
  const size_t INITIAL_BUFFER_SIZE;

  DataInputStream m_input;

  std::vector<char> m_buffer;

  size_t m_have;
  size_t m_pos;

};

#endif // BUFFEREDINPUTSTREAM_H
