meta:
  id: rfb_client
  title: RFB (VNC), client to server, as SquirrelVNC sends it
  file-extension: out
  license: GPL-2.0-or-later
  endian: be
doc: |
  What SquirrelVNC's client sends, in the form of the expected client
  bytes of the replay cases (tests/cases/*.out) or the output of
  rfb-replay: a whole session from the ProtocolVersion message.

  The client's handshake depends on what the server offered, which is in
  the other direction's bytes (rfb_server.ksy). This description assumes:
  - in RFB 3.3, no VNC authentication (the server chose the security
    type, so nothing in these bytes says whether a 16-byte response
    follows);
  - in RFB 3.7 and 3.8, the chosen security type comes first, and a
    VNC response follows it if the type is VNC;
  - the bytes may end after any part of the handshake, when the server
    refused the connection or authentication failed.

  Mirrors RemoteViewerCore (handshake(), negotiateSecurityType(),
  clientAndServerInit()) and the Rfb*ClientMessage classes.
doc-ref: https://www.rfc-editor.org/rfc/rfc6143
seq:
  - id: version
    type: str
    size: 12
    encoding: ASCII
    doc: '"RFB 003.003\n", "RFB 003.007\n" or "RFB 003.008\n"'
  - id: security_type
    type: u1
    enum: security_type
    if: minor >= 7 and not _io.eof
  - id: vnc_response
    size: 16
    if: minor >= 7 and security_type == security_type::vnc and not _io.eof
    doc: The server's challenge, DES-encrypted with the password.
  - id: shared_flag
    type: u1
    if: not _io.eof
    doc: ClientInit; SquirrelVNC sets it unless told not to.
  - id: messages
    type: client_message
    repeat: eos
instances:
  minor:
    value: version.substring(8, 11).to_i

types:
  client_message:
    seq:
      - id: message_type
        type: u1
        enum: client_message_type
      - id: extended_type
        type: b24
        if: message_type == client_message_type::tight_extension
        doc: TightVNC extension messages, 0xFC and three more bytes.
      - id: body
        type:
          switch-on: message_type
          cases:
            'client_message_type::set_pixel_format': set_pixel_format
            'client_message_type::set_encodings': set_encodings
            'client_message_type::framebuffer_update_request': framebuffer_update_request
            'client_message_type::key_event': key_event
            'client_message_type::pointer_event': pointer_event
            'client_message_type::client_cut_text': client_cut_text
            'client_message_type::tight_extension': tight_extension_message(extended_type)
            _: unknown_message

  set_pixel_format:
    seq:
      - id: padding
        size: 3
      - id: pixel_format
        type: pixel_format

  pixel_format:
    seq:
      - id: bits_per_pixel
        type: u1
      - id: depth
        type: u1
      - id: big_endian
        type: u1
      - id: true_colour
        type: u1
      - id: red_max
        type: u2
      - id: green_max
        type: u2
      - id: blue_max
        type: u2
      - id: red_shift
        type: u1
      - id: green_shift
        type: u1
      - id: blue_shift
        type: u1
      - id: padding
        size: 3

  set_encodings:
    seq:
      - id: padding
        size: 1
      - id: num_encodings
        type: u2
      - id: encodings
        type: s4
        enum: encoding
        repeat: expr
        repeat-expr: num_encodings

  framebuffer_update_request:
    seq:
      - id: incremental
        type: u1
      - id: x
        type: u2
      - id: y
        type: u2
      - id: width
        type: u2
      - id: height
        type: u2

  key_event:
    seq:
      - id: down
        type: u1
      - id: padding
        size: 2
      - id: keysym
        type: u4

  pointer_event:
    seq:
      - id: button_mask
        type: u1
      - id: x
        type: u2
      - id: y
        type: u2

  client_cut_text:
    seq:
      - id: padding
        size: 3
      - id: length
        type: u4
      - id: text
        size: length
        doc: Latin-1.

  tight_extension_message:
    params:
      - id: extended_type
        type: u4
    seq:
      - id: utf8_cut_text
        type: utf8_cut_text
        if: extended_type == 0x000200
      - id: rest
        size-eos: true
        if: extended_type != 0x000200 and extended_type != 0x000201
        doc: |
          0xFC000201 (EnableCutTextUtf8) has no body; other extension
          messages (file transfer) are not described here.

  utf8_cut_text:
    doc: TightVNC's ClientCutTextUtf8 (0xFC000200).
    seq:
      - id: length
        type: u4
      - id: text
        size: length

  unknown_message:
    seq:
      - id: rest
        size-eos: true

enums:
  security_type:
    0: invalid
    1: none
    2: vnc
    16: tight

  client_message_type:
    0: set_pixel_format
    2: set_encodings
    3: framebuffer_update_request
    4: key_event
    5: pointer_event
    6: client_cut_text
    0xfc: tight_extension

  encoding:
    0: raw
    1: copy_rect
    2: rre
    5: hextile
    7: tight
    16: zrle
    -239: rich_cursor
    -232: pointer_pos
    -224: last_rect
    -223: desktop_size
    -256: compression_level_0
    -255: compression_level_1
    -254: compression_level_2
    -253: compression_level_3
    -252: compression_level_4
    -251: compression_level_5
    -250: compression_level_6
    -249: compression_level_7
    -248: compression_level_8
    -247: compression_level_9
    -32: jpeg_quality_level_0
    -31: jpeg_quality_level_1
    -30: jpeg_quality_level_2
    -29: jpeg_quality_level_3
    -28: jpeg_quality_level_4
    -27: jpeg_quality_level_5
    -26: jpeg_quality_level_6
    -25: jpeg_quality_level_7
    -24: jpeg_quality_level_8
    -23: jpeg_quality_level_9
