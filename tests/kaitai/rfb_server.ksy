meta:
  id: rfb_server
  title: RFB (VNC), server to client, as SquirrelVNC reads it
  file-extension: rfb
  license: GPL-2.0-or-later
  endian: be
doc: |
  What an RFB server sends to the client, in the form of the replay cases
  in tests/cases/*.rfb (a whole session, from the ProtocolVersion message)
  or of the fuzz inputs in tests/fuzz (from ServerInit on). A file that
  starts with "RFB " is taken as a whole session.

  The layout of the server's messages depends on what the client chose,
  which is in the other direction's bytes (rfb_client.ksy). This
  description assumes what SquirrelVNC's client does in rfb-replay:
  - it takes the first security type the server offers (the viewer
    prefers Tight security if offered, which is not described here, and
    otherwise takes the first type it supports);
  - it never sends SetPixelFormat, so pixels are in the format of
    ServerInit (rfb-replay can be told otherwise; such files decode
    wrongly here);
  - Tight capabilities after ServerInit (Tight security) are not
    described.

  Compressed data (Tight, ZRLE) is shown as opaque bytes: the zlib
  streams run on across rectangles, so a block cannot be inflated on its
  own.

  Mirrors RemoteViewerCore (handshake(), authenticate(),
  clientAndServerInit(), the message loop in execute()) and the decoders
  in core-lib/viewer-core. Where the viewer deviates from RFC 6143, this
  follows the viewer.
doc-ref: https://www.rfc-editor.org/rfc/rfc6143
seq:
  - id: handshake
    type: handshake
    if: is_whole_session
  - id: server_init
    type: server_init
    if: not is_whole_session or handshake.is_accepted
  - id: messages
    type: server_message(server_init.pixel_format)
    repeat: eos
    if: not is_whole_session or handshake.is_accepted
  - id: unparsed
    size-eos: true
    if: is_whole_session and not handshake.is_accepted
    doc: What follows a refused or unsupported handshake.
instances:
  is_whole_session:
    value: '_io.size >= 4 and first_four == "RFB "'
  first_four:
    pos: 0
    size: 4
    type: str
    encoding: ASCII
    if: _io.size >= 4

types:
  handshake:
    doc: |
      ProtocolVersion, then the security handshake (RemoteViewerCore::
      handshake(), negotiateSecurityType(), authenticate()).
    seq:
      - id: version
        type: str
        size: 12
        encoding: ASCII
        doc: '"RFB xxx.yyy\n"'
      - id: security_type_v3_3
        type: u4
        enum: security_type
        if: minor < 7
        doc: RFB 3.3, the server alone chooses.
      - id: num_security_types
        type: u1
        if: minor >= 7
      - id: security_types
        type: u1
        enum: security_type
        repeat: expr
        repeat-expr: num_security_types
        if: minor >= 7
      - id: refusal_reason
        type: rfb_string
        if: is_refused and minor >= 8
        doc: RFB 3.8; the viewer reads no reason in earlier versions.
      - id: vnc_challenge
        size: 16
        if: not is_refused and chosen == security_type::vnc
        doc: The client answers with the challenge DES-encrypted.
      - id: security_result
        type: u4
        if: has_result
        doc: 0 for success.
      - id: failure_reason
        type: rfb_string
        if: has_result and minor >= 8 and security_result != 0
    instances:
      major:
        value: version.substring(4, 7).to_i
      minor_offered:
        value: version.substring(8, 11).to_i
      minor:
        value: 'major > 3 or minor_offered >= 8 ? 8 : (minor_offered >= 7 ? 7 : 3)'
        doc: The minor version the client answers with (3.3, 3.7 or 3.8).
      is_refused:
        value: 'minor >= 7 ? num_security_types == 0 : security_type_v3_3 == security_type::invalid'
      chosen:
        value: 'minor >= 7 ? security_types[0] : security_type_v3_3'
        doc: Assumes the client takes the first type offered.
      has_result:
        value: 'not is_refused and chosen != security_type::tight and (minor >= 8 or chosen != security_type::none)'
      is_accepted:
        value: 'not is_refused and chosen != security_type::tight and (has_result ? security_result == 0 : true)'

  server_init:
    doc: RemoteViewerCore::clientAndServerInit().
    seq:
      - id: width
        type: u2
      - id: height
        type: u2
      - id: pixel_format
        type: pixel_format
      - id: name_length
        type: u4
      - id: name
        size: name_length
        doc: The viewer accepts at most 64 KiB.

  pixel_format:
    seq:
      - id: bits_per_pixel
        type: u1
        doc: The viewer accepts 8, 16 or 32.
      - id: depth
        type: u1
      - id: big_endian
        type: u1
      - id: true_colour
        type: u1
        doc: |
          0 means a colour map, which the viewer does not support: it
          uses a standard true-colour format of the same size instead.
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
    instances:
      bytes_per_pixel:
        value: bits_per_pixel / 8
      is_tight_cpixel:
        value: |
          bits_per_pixel == 32 and
          (true_colour == 0 or
           (depth == 24 and red_max == 255 and green_max == 255 and blue_max == 255))
        doc: |
          Tight sends 3-byte pixels for this format (TightDecoder::decode()).
          For a colour-map format the viewer's stand-in is RGB888.
      tight_pixel_size:
        value: 'is_tight_cpixel ? 3 : bytes_per_pixel'

  server_message:
    params:
      - id: pf
        type: pixel_format
    seq:
      - id: message_type
        type: u1
        enum: server_message_type
      - id: extended_type
        type: b24
        if: message_type == server_message_type::tight_extension
        doc: |
          TightVNC extension messages: 0xFC followed by three more bytes
          of the type (RemoteViewerCore::receiveServerMessageType()).
      - id: body
        type:
          switch-on: message_type
          cases:
            'server_message_type::framebuffer_update': framebuffer_update(pf)
            'server_message_type::set_colour_map_entries': set_colour_map_entries
            'server_message_type::bell': bell
            'server_message_type::server_cut_text': server_cut_text
            'server_message_type::tight_extension': tight_extension_message(extended_type)
            _: unknown_message

  framebuffer_update:
    doc: RemoteViewerCore::receiveFbUpdate().
    params:
      - id: pf
        type: pixel_format
    seq:
      - id: padding
        size: 1
      - id: num_rectangles
        type: u2
      - id: rectangles
        type: rectangle(pf)
        repeat: until
        repeat-until: '_.encoding == encoding::last_rect or _index + 1 >= num_rectangles'
        if: num_rectangles > 0
        doc: A LastRect pseudo-rectangle ends the update early.

  rectangle:
    doc: RemoteViewerCore::receiveFbUpdateRectangle().
    params:
      - id: pf
        type: pixel_format
    seq:
      - id: x
        type: u2
      - id: y
        type: u2
      - id: width
        type: u2
      - id: height
        type: u2
      - id: encoding
        type: s4
        enum: encoding
      - id: data
        type:
          switch-on: encoding
          cases:
            'encoding::raw': raw_data(width, height, pf)
            'encoding::copy_rect': copy_rect_data
            'encoding::rre': rre_data(pf)
            'encoding::hextile': hextile_data(width, height, pf)
            'encoding::tight': tight_data(width, height, pf)
            'encoding::zrle': zrle_data
            'encoding::rich_cursor': rich_cursor_data(width, height, pf)
            'encoding::pointer_pos': empty
            'encoding::last_rect': empty
            'encoding::desktop_size': empty
            _: unknown_encoding

  empty:
    seq: []

  unknown_encoding:
    doc: The viewer stops here with an error; the rest is not parsed.
    seq:
      - id: rest
        size-eos: true

  raw_data:
    params:
      - id: width
        type: u2
      - id: height
        type: u2
      - id: pf
        type: pixel_format
    seq:
      - id: rows
        size: width * pf.bytes_per_pixel
        repeat: expr
        repeat-expr: height

  copy_rect_data:
    seq:
      - id: src_x
        type: s2
      - id: src_y
        type: s2

  rre_data:
    doc: RreDecoder::decode(). Subrectangles must lie within the rectangle.
    params:
      - id: pf
        type: pixel_format
    seq:
      - id: num_subrectangles
        type: u4
      - id: background
        size: pf.bytes_per_pixel
      - id: subrectangles
        type: rre_subrectangle(pf)
        repeat: expr
        repeat-expr: num_subrectangles

  rre_subrectangle:
    params:
      - id: pf
        type: pixel_format
    seq:
      - id: colour
        size: pf.bytes_per_pixel
      - id: x
        type: u2
      - id: y
        type: u2
      - id: width
        type: u2
      - id: height
        type: u2

  hextile_data:
    doc: |
      HexTileDecoder::decode(): 16 x 16 tiles, left to right then top to
      bottom; those at the right and bottom edges may be smaller.
    params:
      - id: width
        type: u2
      - id: height
        type: u2
      - id: pf
        type: pixel_format
    seq:
      - id: tiles
        type: |
          hextile_tile(
            _index % tiles_across == tiles_across - 1 ? width - 16 * (tiles_across - 1) : 16,
            _index / tiles_across == tiles_down - 1 ? height - 16 * (tiles_down - 1) : 16,
            pf)
        repeat: expr
        repeat-expr: tiles_across * tiles_down
    instances:
      tiles_across:
        value: (width + 15) / 16
      tiles_down:
        value: (height + 15) / 16

  hextile_tile:
    params:
      - id: tile_width
        type: u2
      - id: tile_height
        type: u2
      - id: pf
        type: pixel_format
    seq:
      - id: flags
        type: u1
        doc: |
          1 raw, 2 background specified, 4 foreground specified, 8 any
          subrectangles, 16 subrectangles coloured.
      - id: raw_pixels
        size: tile_width * tile_height * pf.bytes_per_pixel
        if: is_raw
      - id: background
        size: pf.bytes_per_pixel
        if: not is_raw and flags & 2 != 0
      - id: foreground
        size: pf.bytes_per_pixel
        if: not is_raw and flags & 4 != 0
      - id: num_subrectangles
        type: u1
        if: not is_raw and flags & 8 != 0
      - id: subrectangles
        type: hextile_subrectangle(has_subrectangle_colour, pf)
        repeat: expr
        repeat-expr: num_subrectangles
        if: not is_raw and flags & 8 != 0
    instances:
      is_raw:
        value: flags & 1 != 0
      has_subrectangle_colour:
        value: flags & 16 != 0 and flags & 4 == 0
        doc: |
          As the viewer reads it: with "foreground specified" also set,
          it reads no colour per subrectangle.

  hextile_subrectangle:
    params:
      - id: has_colour
        type: bool
      - id: pf
        type: pixel_format
    seq:
      - id: colour
        size: pf.bytes_per_pixel
        if: has_colour
      - id: xy
        type: u1
      - id: wh
        type: u1
    instances:
      x:
        value: xy >> 4
      y:
        value: xy & 15
      width:
        value: (wh >> 4) + 1
      height:
        value: (wh & 15) + 1

  tight_data:
    doc: TightDecoder::decode() and processBasicTypes().
    params:
      - id: width
        type: u2
      - id: height
        type: u2
      - id: pf
        type: pixel_format
    seq:
      - id: control
        type: u1
        doc: |
          Bits 0-3 reset zlib streams 0-3. Bits 4-7: 8 fill, 9 JPEG,
          0-7 basic compression, where bit 6 means a filter id follows
          and bits 4-5 are the zlib stream. Above 9 the viewer stops.
      - id: fill_colour
        size: pf.tight_pixel_size
        if: compression == 8
      - id: jpeg_length
        type: tight_compact_length
        if: compression == 9
      - id: jpeg_data
        size: jpeg_length.value
        if: compression == 9
      - id: filter
        type: u1
        enum: tight_filter
        if: is_basic and control & 0x40 != 0
      - id: palette_size_minus_1
        type: u1
        if: is_basic and active_filter == tight_filter::palette
      - id: palette
        size: pf.tight_pixel_size
        repeat: expr
        repeat-expr: palette_size_minus_1 + 1
        if: is_basic and active_filter == tight_filter::palette
      - id: raw_pixels
        size: data_length
        if: is_basic and is_known_filter and data_length < 12
        doc: Fewer than 12 bytes are sent uncompressed.
      - id: zlib_length
        type: tight_compact_length
        if: is_basic and is_known_filter and data_length >= 12
      - id: zlib_data
        size: zlib_length.value
        if: is_basic and is_known_filter and data_length >= 12
    instances:
      compression:
        value: control >> 4
      is_basic:
        value: compression < 8
      zlib_stream:
        value: (control >> 4) & 3
      active_filter:
        value: 'control & 0x40 != 0 ? filter : tight_filter::copy'
      is_known_filter:
        value: |
          active_filter == tight_filter::copy or
          active_filter == tight_filter::palette or
          active_filter == tight_filter::gradient
        doc: For an unknown filter the viewer reads nothing more.
      data_length:
        value: |
          active_filter == tight_filter::palette
            ? (palette_size_minus_1 == 1 ? (width + 7) / 8 * height : width * height)
            : width * height * pf.tight_pixel_size
        doc: Length of the data before compression.

  tight_compact_length:
    doc: 1 to 3 bytes, 7 bits each, low bits first (readCompactSize()).
    seq:
      - id: b0
        type: u1
      - id: b1
        type: u1
        if: b0 & 0x80 != 0
      - id: b2
        type: u1
        if: b0 & 0x80 != 0 and b1 & 0x80 != 0
    instances:
      value:
        value: |
          (b0 & 0x7f)
          + (b0 & 0x80 != 0 ? (b1 & 0x7f) << 7 : 0)
          + (b0 & 0x80 != 0 and b1 & 0x80 != 0 ? b2 << 14 : 0)

  zrle_data:
    doc: ZrleDecoder::readAndInflate(); the tiles are inside the zlib data.
    seq:
      - id: length
        type: u4
      - id: zlib_data
        size: length

  rich_cursor_data:
    doc: |
      RemoteViewerCore::processPseudoEncoding(): pixels then a bitmask,
      both absent if the cursor is empty. The viewer accepts at most
      1024 x 1024.
    params:
      - id: width
        type: u2
      - id: height
        type: u2
      - id: pf
        type: pixel_format
    seq:
      - id: pixels
        size: width * height * pf.bytes_per_pixel
      - id: bitmask
        size: (width + 7) / 8 * height
        if: width * height != 0

  set_colour_map_entries:
    doc: Read and ignored by the viewer.
    seq:
      - id: padding
        size: 1
      - id: first_colour
        type: u2
      - id: num_colours
        type: u2
      - id: colours
        size: 6
        repeat: expr
        repeat-expr: num_colours

  bell:
    seq: []

  server_cut_text:
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
        if: extended_type != 0x000200
        doc: |
          Other extension messages go to capability handlers (file
          transfer), not described here.

  utf8_cut_text:
    doc: TightVNC's ServerCutTextUtf8 (0xFC000200).
    seq:
      - id: length
        type: u4
      - id: text
        size: length

  unknown_message:
    doc: |
      The viewer logs an unknown type and reads on from the next byte;
      here the rest is shown unparsed.
    seq:
      - id: rest
        size-eos: true

  rfb_string:
    seq:
      - id: length
        type: u4
      - id: value
        size: length
        doc: UTF-8 in RFB 3.8 reason strings.

enums:
  security_type:
    0: invalid
    1: none
    2: vnc
    16: tight

  server_message_type:
    0: framebuffer_update
    1: set_colour_map_entries
    2: bell
    3: server_cut_text
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

  tight_filter:
    0: copy
    1: palette
    2: gradient
