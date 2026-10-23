# Copyright (C) 2026 Steve Cheng.
#
# -------------------------------------------------------------------------
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/).
#
# This program is free software; you can redistribute it and/or modify
# it under the terms of the GNU General Public License as published by
# the Free Software Foundation; either version 2 of the License, or
# (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License along
# with this program; if not, write to the Free Software Foundation, Inc.,
# 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
# -------------------------------------------------------------------------

"""Generator library for the RFB replay test cases.

A case is written as a script of what the server sends. For each step,
the library also records what SquirrelVNC's client must send back, the
events the replay harness must log, and the effect on a model of the
framebuffer. Case.files() then gives, relative to tests/:

    cases/NAME.rfb      server-to-client bytes (the harness's input)
    cases/NAME.out      expected client-to-server bytes
    cases/NAME.events   expected event log
    cases/NAME.ppm      expected final framebuffer
    fuzz/corpus/NAME.corpus  the server bytes from ServerInit on, if any

The expected client bytes follow RemoteViewerCore as rfb-replay runs it:
shared flag set, no SetPixelFormat unless the case sets a client pixel
format, the default encodings, a full update request after ServerInit
and an incremental one after each update. See notes/testing.md.

Only the Python 3 standard library is used.
"""

import os
import struct
import zlib

# Encoding numbers (rfb/EncodingDefs.h).
RAW = 0
COPYRECT = 1
RRE = 2
HEXTILE = 5
TIGHT = 7
ZRLE = 16
RICH_CURSOR = -239
POINTER_POS = -232
LAST_RECT = -224
DESKTOP_SIZE = -223

# Security types (rfb/AuthDefs.h).
SEC_INVALID = 0
SEC_NONE = 1
SEC_VNC = 2
SEC_TIGHT = 16

# What RemoteViewerCore sends in SetEncodings by default: the preferred
# encoding (Tight) first, then the other decoders that RemoteViewerCore::init()
# registers, by descending priority and, for equal priority, descending
# number (DecoderStore::getDecoderIds()).
DEFAULT_ENCODINGS = [TIGHT, COPYRECT, ZRLE, HEXTILE, RRE, RAW,
                     DESKTOP_SIZE, LAST_RECT, POINTER_POS, RICH_CURSOR]

# The message ByteArrayInputStream throws at the end of the input.
END_OF_STREAM = 'End of stream reached'


def u8(v):
    return struct.pack('>B', v)


def u16(v):
    return struct.pack('>H', v)


def u32(v):
    return struct.pack('>I', v)


def s32(v):
    return struct.pack('>i', v)


def text_bytes(text):
    """Text as the server sends it: bytes as given, str as UTF-8."""
    return text if isinstance(text, bytes) else text.encode('utf-8')


def rfb_text(data):
    """What the viewer makes of text from the server.

    The rule decided in notes/string-encoding.md: UTF-8 if the bytes are
    valid UTF-8, otherwise Latin-1. Until that work is done, the viewer
    decodes the desktop name and plain cut text with the Windows ANSI
    code page instead, so cases with non-ASCII text in those must wait
    for it; ASCII, and valid UTF-8 where the viewer already reads UTF-8,
    agree with both.
    """
    try:
        return data.decode('utf-8')
    except UnicodeDecodeError:
        return data.decode('latin-1')


def rfb_string(text):
    """A string with a 32-bit length, as in reason strings."""
    data = text_bytes(text)
    return u32(len(data)) + data


class PixelFormat:
    """An RFB true-colour pixel format."""

    def __init__(self, bpp, depth, big_endian, maxes, shifts):
        self.bpp = bpp
        self.depth = depth
        self.big_endian = big_endian
        self.maxes = tuple(maxes)
        self.shifts = tuple(shifts)

    def pack(self, true_colour=True):
        """The 16-byte PIXEL_FORMAT structure."""
        return (u8(self.bpp) + u8(self.depth) + u8(1 if self.big_endian else 0)
                + u8(1 if true_colour else 0)
                + u16(self.maxes[0]) + u16(self.maxes[1]) + u16(self.maxes[2])
                + u8(self.shifts[0]) + u8(self.shifts[1]) + u8(self.shifts[2])
                + b'\0\0\0')

    def from_rgb8(self, rgb):
        """Components in this format for an 8-bit-per-channel colour."""
        return tuple((c * m + 127) // 255 for c, m in zip(rgb, self.maxes))

    def to_rgb8(self, comps):
        """8-bit channels for components in this format.

        This is the conversion rfb-replay uses for its PPM output, and it
        must stay the same: c * 255 / max, rounded to nearest.
        """
        return tuple((c * 255 + m // 2) // m for c, m in zip(comps, self.maxes))

    def pixel_bytes(self, comps):
        """One pixel, as the server sends it in Raw data."""
        value = 0
        for c, s in zip(comps, self.shifts):
            value |= c << s
        return value.to_bytes(self.bpp // 8,
                              'big' if self.big_endian else 'little')


# Standard formats, as the servers in common use send them.
RGB888 = PixelFormat(32, 24, False, (255, 255, 255), (16, 8, 0))
RGB565 = PixelFormat(16, 16, False, (31, 63, 31), (11, 5, 0))


class Image:
    """A width x height array of 8-bit RGB colours, for a case to draw."""

    def __init__(self, width, height, fill=(0, 0, 0)):
        self.width = width
        self.height = height
        self.rows = [[fill] * width for _ in range(height)]

    def put(self, x, y, rgb):
        self.rows[y][x] = rgb

    def get(self, x, y):
        return self.rows[y][x]

    @staticmethod
    def pattern(width, height, seed=0):
        """An image with a different colour at every pixel.

        Every channel varies, with x, with y and between channels, so that
        swapped channels, wrong byte order and misplaced rows all show.
        """
        image = Image(width, height)
        for y in range(height):
            for x in range(width):
                image.put(x, y, ((x * 7 + seed * 31) % 256,
                                 (y * 11 + seed * 17) % 256,
                                 (x * 3 + y * 5 + seed * 13) % 256))
        return image


def load_ppm(path):
    """An Image from a binary PPM (P6, maximum 255), such as rfb-replay's."""
    data = open(path, 'rb').read()
    parts = data.split(b'\n', 3)
    assert parts[0] == b'P6' and parts[2] == b'255', path
    width, height = (int(v) for v in parts[1].split())
    pixels = parts[3]
    assert len(pixels) == width * height * 3, path
    image = Image(width, height)
    for y in range(height):
        for x in range(width):
            i = (y * width + x) * 3
            image.put(x, y, tuple(pixels[i:i + 3]))
    return image


class FrameBuffer:
    """The model of the client's framebuffer: components per pixel."""

    def __init__(self, width, height, pf):
        self.width = width
        self.height = height
        self.pf = pf
        # setFbProperties() fills a new framebuffer with black.
        self.rows = [[(0, 0, 0)] * width for _ in range(height)]

    def fill(self, x, y, w, h, comps):
        """Fill a rectangle, clipped to the framebuffer."""
        for row in range(max(0, y), min(self.height, y + h)):
            for col in range(max(0, x), min(self.width, x + w)):
                self.rows[row][col] = comps

    def ppm(self):
        out = bytearray(b'P6\n%d %d\n255\n' % (self.width, self.height))
        for row in self.rows:
            for comps in row:
                out += bytes(self.pf.to_rgb8(comps))
        return bytes(out)


EMPTY_PPM = b'P6\n0 0\n255\n'


class RawRect:
    """A rectangle in Raw encoding, taken from an image."""

    def __init__(self, x, y, w, h, image, src_x=None, src_y=None):
        self.x, self.y, self.w, self.h = x, y, w, h
        self.image = image
        self.src_x = x if src_x is None else src_x
        self.src_y = y if src_y is None else src_y

    def encode(self, fb):
        out = bytearray(u16(self.x) + u16(self.y) + u16(self.w) + u16(self.h)
                        + s32(RAW))
        for dy in range(self.h):
            for dx in range(self.w):
                rgb = self.image.get(self.src_x + dx, self.src_y + dy)
                comps = fb.pf.from_rgb8(rgb)
                out += fb.pf.pixel_bytes(comps)
                fb.rows[self.y + dy][self.x + dx] = comps
        return bytes(out)


class CopyRect:
    """A rectangle in CopyRect encoding: copy (src_x, src_y) of the frame
    buffer, as it stands after the rectangles before it, to the
    rectangle's position.

    The viewer copies within the frame buffer as if from a snapshot of
    it, whichever way the two areas overlap (FrameBuffer::move()), and
    copies only where both areas lie in the frame buffer. Unlike the
    other decoders, it copies into the frame buffer directly, not into
    the rectangle buffer (CopyRectDecoder::copy()).
    """

    def __init__(self, x, y, w, h, src_x, src_y):
        self.x, self.y, self.w, self.h = x, y, w, h
        self.src_x, self.src_y = src_x, src_y

    def encode(self, fb):
        assert 0 <= self.src_x < 32768 and 0 <= self.src_y < 32768, \
            'the viewer reads the source position as signed 16-bit'
        snapshot = [list(row) for row in fb.rows]
        for dy in range(self.h):
            for dx in range(self.w):
                x, y = self.x + dx, self.y + dy
                sx, sy = self.src_x + dx, self.src_y + dy
                if (0 <= x < fb.width and 0 <= y < fb.height and
                        0 <= sx < fb.width and 0 <= sy < fb.height):
                    fb.rows[y][x] = snapshot[sy][sx]
        return (u16(self.x) + u16(self.y) + u16(self.w) + u16(self.h)
                + s32(COPYRECT) + u16(self.src_x) + u16(self.src_y))


class RreRect:
    """A rectangle in RRE encoding: a background, then subrectangles.

    Subrectangles are given relative to the rectangle, as (x, y, width,
    height, rgb). The viewer must reject one that does not lie within the
    rectangle (RreDecoder::decode()), and error is set to the message the
    session ends with. Nothing of that rectangle then reaches the frame
    buffer: decoders draw into a separate rectangle buffer, which
    DecoderOfRectangle::process() copies only after decoding succeeds.
    """

    ERROR = 'Error in protocol: RRE subrectangle outside its rectangle'

    def __init__(self, x, y, w, h, background, subrects=()):
        self.x, self.y, self.w, self.h = x, y, w, h
        self.background = background
        self.subrects = list(subrects)
        self.error = None

    def encode(self, fb):
        pf = fb.pf
        out = bytearray(u16(self.x) + u16(self.y) + u16(self.w) + u16(self.h)
                        + s32(RRE) + u32(len(self.subrects)))
        out += pf.pixel_bytes(pf.from_rgb8(self.background))
        fills = [(self.x, self.y, self.w, self.h, pf.from_rgb8(self.background))]
        for sx, sy, sw, sh, rgb in self.subrects:
            comps = pf.from_rgb8(rgb)
            out += pf.pixel_bytes(comps) + u16(sx) + u16(sy) + u16(sw) + u16(sh)
            if sx + sw > self.w or sy + sh > self.h:
                self.error = self.ERROR
            fills.append((self.x + sx, self.y + sy, sw, sh, comps))
        if self.error is None:
            for fill in fills:
                fb.fill(*fill)
        return bytes(out)


class HextileTile:
    """One Hextile tile, as a HextileRect sends it.

    Either raw (the tile's pixels, taken from an image at the tile's
    position), or a background and subrectangles. background and
    foreground are 8-bit RGB colours, or None to carry on with the
    previous tile's. Subrectangles are (x, y, width, height, rgb) relative
    to the tile, with rgb None for the foreground colour; coloured sends a
    colour with each subrectangle instead.
    """

    def __init__(self, raw=None, background=None, foreground=None,
                 subrects=(), coloured=False):
        assert not (coloured and foreground is not None), \
            'the viewer reads no subrectangle colours when both are set'
        self.raw = raw
        self.background = background
        self.foreground = foreground
        self.subrects = list(subrects)
        self.coloured = coloured


class HextileRect:
    """A rectangle in Hextile encoding: 16 x 16 tiles, left to right then
    top to bottom, smaller at the right and bottom edges.

    tiles holds one HextileTile per tile. A subrectangle that does not lie
    within its tile must make the viewer stop (HexTileDecoder::decode());
    error is then set, and, as for RreRect, nothing of the rectangle
    reaches the frame buffer.
    """

    ERROR = 'Error in protocol: Hextile subrectangle outside its tile'

    def __init__(self, x, y, w, h, tiles):
        self.x, self.y, self.w, self.h = x, y, w, h
        self.tiles = list(tiles)
        self.error = None

    def tile_rects(self):
        for ty in range(self.y, self.y + self.h, 16):
            for tx in range(self.x, self.x + self.w, 16):
                yield (tx, ty, min(16, self.x + self.w - tx),
                       min(16, self.y + self.h - ty))

    def encode(self, fb):
        pf = fb.pf
        rects = list(self.tile_rects())
        assert len(rects) == len(self.tiles), \
            'expected %d tiles, got %d' % (len(rects), len(self.tiles))
        out = bytearray(u16(self.x) + u16(self.y) + u16(self.w) + u16(self.h)
                        + s32(HEXTILE))
        fills = []
        background = foreground = None
        for (tx, ty, tw, th), tile in zip(rects, self.tiles):
            if tile.raw is not None:
                out += u8(1)
                for row in range(ty, ty + th):
                    for col in range(tx, tx + tw):
                        comps = pf.from_rgb8(tile.raw.get(col, row))
                        out += pf.pixel_bytes(comps)
                        fills.append((col, row, 1, 1, comps))
                continue
            flags = ((2 if tile.background is not None else 0)
                     | (4 if tile.foreground is not None else 0)
                     | (8 if tile.subrects else 0)
                     | (16 if tile.coloured else 0))
            out += u8(flags)
            if tile.background is not None:
                background = tile.background
                out += pf.pixel_bytes(pf.from_rgb8(background))
            assert background is not None, 'no background colour yet'
            fills.append((tx, ty, tw, th, pf.from_rgb8(background)))
            if tile.foreground is not None:
                foreground = tile.foreground
                out += pf.pixel_bytes(pf.from_rgb8(foreground))
            if tile.subrects:
                out += u8(len(tile.subrects))
            for sx, sy, sw, sh, rgb in tile.subrects:
                assert 0 <= sx < 16 and 0 <= sy < 16 and 1 <= sw <= 16 and 1 <= sh <= 16
                if tile.coloured:
                    colour = rgb
                    out += pf.pixel_bytes(pf.from_rgb8(rgb))
                else:
                    colour = foreground
                    assert colour is not None, 'no foreground colour yet'
                out += u8((sx << 4) | sy) + u8(((sw - 1) << 4) | (sh - 1))
                if sx + sw > tw or sy + sh > th:
                    if self.error is None:
                        self.error = self.ERROR
                fills.append((tx + sx, ty + sy, sw, sh, pf.from_rgb8(colour)))
        if self.error is None:
            for fill in fills:
                fb.fill(*fill)
        return bytes(out)


class TightStreams:
    """The four zlib streams of a Tight session, as the server keeps them.

    Each stream's state carries on across rectangles, as the viewer's
    inflaters do, until a rectangle's control byte resets it. One object
    serves every TightRect of a session (Case.tight_streams()).

    The compressed bytes depend on the zlib build: with another one,
    cases.py --check may report Tight cases as differing although they
    decode the same.
    """

    def __init__(self):
        self.streams = [None] * 4

    def reset(self, mask):
        for i in range(4):
            if mask & (1 << i):
                self.streams[i] = None

    def compress(self, stream, data):
        if self.streams[stream] is None:
            self.streams[stream] = zlib.compressobj(6)
        c = self.streams[stream]
        return c.compress(data) + c.flush(zlib.Z_SYNC_FLUSH)


def tight_compact_length(n):
    """1 to 3 bytes, 7 bits each, low bits first."""
    assert 0 <= n < 1 << 22
    if n < 0x80:
        return u8(n)
    if n < 0x4000:
        return u8(0x80 | (n & 0x7f)) + u8(n >> 7)
    return u8(0x80 | (n & 0x7f)) + u8(0x80 | ((n >> 7) & 0x7f)) + u8(n >> 14)


def tight_is_cpixel(pf):
    """TightDecoder::decode(): 3-byte pixels for 32-bit, depth-24 RGB."""
    return pf.bpp == 32 and pf.depth == 24 and pf.maxes == (255, 255, 255)


def tight_pixel(pf, comps):
    """A Tight pixel: R, G, B bytes for 3-byte pixels, else the pixel.

    The viewer swaps the 3 bytes into its 32-bit layout, which is right
    only with red at shift 16 (as in RGB888); the cases use such formats.
    """
    if tight_is_cpixel(pf):
        assert pf.shifts == (16, 8, 0) and not pf.big_endian
        return bytes(comps)
    return pf.pixel_bytes(comps)


class TightRect:
    """A rectangle in Tight encoding.

    kind is one of:
      ('fill', rgb)
      ('copy', image)              pixels from the image at the rectangle
      ('palette', colours, rows)   rows[y][x] indexes colours (2 to 256)
      ('gradient', image)          the gradient filter
      ('jpeg', data, decoded)      JPEG data, and the image the viewer's
                                   decoder makes of it (an Image of the
                                   rectangle's size), or None if the
                                   viewer must ignore it (see below)
    stream (0 to 3) is the zlib stream for data of 12 bytes or more, and
    reset a mask of streams to reset first. shorten makes the compressed
    data inflate to that many bytes fewer than the rectangle needs, which
    the viewer must reject. compression overrides the control byte's
    compression type.

    As for RreRect, a rectangle the viewer rejects (error) leaves nothing
    in the frame buffer.

    A JPEG the viewer cannot decode is not an error: TightDecoder::
    processJpeg() logs it and goes on, and the rectangle gets what the
    rectangle buffer held there. That is what the frame buffer showed,
    unless a CopyRect changed the area since (CopyRect writes to the
    frame buffer only), so such a case must not follow a CopyRect over
    the same area. The model leaves the frame buffer unchanged.
    """

    BAD_TYPE = 'Sub-encoding in Tight-encoder are not valid'
    BAD_SIZE = 'Error in protocol: incorrect decompressed data size (tight-decoder)'

    def __init__(self, x, y, w, h, kind, streams, stream=0, reset=0,
                 shorten=0, compression=None):
        self.x, self.y, self.w, self.h = x, y, w, h
        self.kind = kind
        self.streams = streams
        self.stream = stream
        self.reset = reset
        self.shorten = shorten
        self.compression = compression
        self.error = None

    def encode(self, fb):
        pf = fb.pf
        out = bytearray(u16(self.x) + u16(self.y) + u16(self.w) + u16(self.h)
                        + s32(TIGHT))
        self.streams.reset(self.reset)
        what = self.kind[0]
        pixels = {}   # (x, y) -> components, drawn if accepted

        if self.compression is not None:
            out += u8((self.compression << 4) | self.reset)
            self.error = self.BAD_TYPE
            return bytes(out)

        if what == 'jpeg':
            data, decoded = self.kind[1], self.kind[2]
            out += u8(0x90 | self.reset) + tight_compact_length(len(data)) + data
            if decoded is not None:
                assert (decoded.width, decoded.height) == (self.w, self.h)
                for y in range(self.h):
                    for x in range(self.w):
                        pixels[(x, y)] = pf.from_rgb8(decoded.get(x, y))
        elif what == 'fill':
            comps = pf.from_rgb8(self.kind[1])
            out += u8(0x80 | self.reset) + tight_pixel(pf, comps)
            for y in range(self.h):
                for x in range(self.w):
                    pixels[(x, y)] = comps
        else:
            if what == 'copy':
                image = self.kind[1]
                filter_id = None
                data = bytearray()
                for y in range(self.h):
                    for x in range(self.w):
                        comps = pf.from_rgb8(image.get(self.x + x, self.y + y))
                        data += tight_pixel(pf, comps)
                        pixels[(x, y)] = comps
            elif what == 'palette':
                colours, rows = self.kind[1], self.kind[2]
                assert 2 <= len(colours) <= 256
                filter_id = 1
                palette = [pf.from_rgb8(c) for c in colours]
                data = bytearray()
                for y in range(self.h):
                    if len(colours) == 2:
                        byte, bits = 0, 0
                        for x in range(self.w):
                            byte = (byte << 1) | rows[y][x]
                            bits += 1
                            if bits == 8:
                                data.append(byte)
                                byte, bits = 0, 0
                        if bits:
                            data.append(byte << (8 - bits))
                    else:
                        data += bytes(rows[y][:self.w])
                    for x in range(self.w):
                        pixels[(x, y)] = palette[rows[y][x]]
            elif what == 'gradient':
                image = self.kind[1]
                filter_id = 2
                values = [[pf.from_rgb8(image.get(self.x + x, self.y + y))
                           for x in range(self.w)] for y in range(self.h)]
                data = bytearray()
                for y in range(self.h):
                    for x in range(self.w):
                        diff = []
                        for i, m in enumerate(pf.maxes):
                            up = values[y - 1][x][i] if y > 0 else 0
                            left = values[y][x - 1][i] if x > 0 else 0
                            diag = values[y - 1][x - 1][i] if x > 0 and y > 0 else 0
                            predicted = min(max(up + left - diag, 0), m)
                            diff.append((values[y][x][i] - predicted) & m)
                        data += tight_pixel(pf, diff)
                        pixels[(x, y)] = values[y][x]
            else:
                raise ValueError(what)

            control = (self.stream << 4) | self.reset
            if filter_id is not None:
                control |= 0x40
            out += u8(control)
            if filter_id is not None:
                out += u8(filter_id)
            if what == 'palette':
                out += u8(len(colours) - 1)
                for comps in palette:
                    out += tight_pixel(pf, comps)
            if len(data) < 12:
                assert not self.shorten, 'only compressed data can be shortened'
                out += data
            else:
                if self.shorten:
                    data = data[:-self.shorten]
                    self.error = self.BAD_SIZE
                packed = self.streams.compress(self.stream, bytes(data))
                out += tight_compact_length(len(packed)) + packed

        if self.error is None:
            for (x, y), comps in pixels.items():
                fb.rows[self.y + y][self.x + x] = comps
        return bytes(out)


class ZrleStream:
    """The one zlib stream of ZRLE, which lasts the whole session, as the
    viewer's ZrleDecoder keeps one inflater for it (Case.zrle_stream()).

    As for TightStreams, the compressed bytes depend on the zlib build.
    """

    def __init__(self):
        self.stream = zlib.compressobj(6)

    def compress(self, data):
        return self.stream.compress(data) + self.stream.flush(zlib.Z_SYNC_FLUSH)


def zrle_cpixel(pf, comps):
    """A ZRLE pixel: the low 3 bytes of a 32-bit pixel whose colours fit
    in them (as for RGB888), else the whole pixel (ZrleDecoder::decode())."""
    data = pf.pixel_bytes(comps)
    if pf.bpp == 32:
        assert not pf.big_endian and all(
            (m << s) < 1 << 24 for m, s in zip(pf.maxes, pf.shifts))
        return data[:3]
    return data


def zrle_run_length(n):
    """A run length: bytes of 255 while needed, then the rest, less one."""
    assert n >= 1
    n -= 1
    out = bytearray()
    while n >= 255:
        out.append(255)
        n -= 255
    out.append(n)
    return bytes(out)


class ZrleTile:
    """One ZRLE tile, as a ZrleRect sends it.

    kind is one of:
      ('raw', image)              pixels from the image at the tile
      ('solid', rgb)
      ('packed', colours, rows)   packed palette of 2 to 16 colours,
                                  rows[y][x] indexing them
      ('plain-rle', runs)         [(rgb, length), ...] in reading order
      ('palette-rle', colours, runs)
                                  [(index, length), ...]; a length of 1
                                  is sent as a single index
    """

    def __init__(self, *kind):
        self.kind = kind


class ZrleRect:
    """A rectangle in ZRLE encoding: 64 x 64 tiles, left to right then
    top to bottom, smaller at the right and bottom edges, all in one
    block of zlib data on the session's stream (Case.zrle_stream()).

    shorten drops that many bytes from the end of the data before it is
    compressed, so that it inflates short. The viewer then fails on
    reading past the inflated data, which raises an IOException, so the
    session ends with a disconnect event ("End of stream reached"), not
    an error; disconnect is set to its message. As for the other
    encodings, a rectangle the viewer does not finish leaves nothing in
    the frame buffer.
    """

    def __init__(self, x, y, w, h, tiles, stream, shorten=0):
        self.x, self.y, self.w, self.h = x, y, w, h
        self.tiles = list(tiles)
        self.stream = stream
        self.shorten = shorten
        self.error = None
        self.disconnect = None

    def tile_rects(self):
        for ty in range(self.y, self.y + self.h, 64):
            for tx in range(self.x, self.x + self.w, 64):
                yield (tx, ty, min(64, self.x + self.w - tx),
                       min(64, self.y + self.h - ty))

    def encode(self, fb):
        pf = fb.pf
        rects = list(self.tile_rects())
        assert len(rects) == len(self.tiles), \
            'expected %d tiles, got %d' % (len(rects), len(self.tiles))
        data = bytearray()
        pixels = {}
        for (tx, ty, tw, th), tile in zip(rects, self.tiles):
            kind = tile.kind
            order = [(tx + i % tw, ty + i // tw) for i in range(tw * th)]
            if kind[0] == 'raw':
                data.append(0)
                for x, y in order:
                    comps = pf.from_rgb8(kind[1].get(x, y))
                    data += zrle_cpixel(pf, comps)
                    pixels[(x, y)] = comps
            elif kind[0] == 'solid':
                comps = pf.from_rgb8(kind[1])
                data.append(1)
                data += zrle_cpixel(pf, comps)
                for xy in order:
                    pixels[xy] = comps
            elif kind[0] == 'packed':
                colours, rows = kind[1], kind[2]
                n = len(colours)
                assert 2 <= n <= 16
                bits = 1 if n == 2 else 2 if n <= 4 else 4
                palette = [pf.from_rgb8(c) for c in colours]
                data.append(n)
                for comps in palette:
                    data += zrle_cpixel(pf, comps)
                for row in range(th):
                    byte, used = 0, 0
                    for col in range(tw):
                        index = rows[row][col]
                        byte = (byte << bits) | index
                        used += bits
                        if used == 8:
                            data.append(byte)
                            byte, used = 0, 0
                        pixels[(tx + col, ty + row)] = palette[index]
                    if used:
                        data.append(byte << (8 - used))
            elif kind[0] == 'plain-rle':
                data.append(128)
                i = 0
                for rgb, length in kind[1]:
                    comps = pf.from_rgb8(rgb)
                    data += zrle_cpixel(pf, comps) + zrle_run_length(length)
                    for xy in order[i:i + length]:
                        pixels[xy] = comps
                    i += length
                assert i == len(order), 'runs must cover the tile'
            elif kind[0] == 'palette-rle':
                colours, runs = kind[1], kind[2]
                assert 2 <= len(colours) <= 127
                palette = [pf.from_rgb8(c) for c in colours]
                data.append(128 + len(colours))
                for comps in palette:
                    data += zrle_cpixel(pf, comps)
                i = 0
                for index, length in runs:
                    if length == 1:
                        data.append(index)
                    else:
                        data.append(128 + index)
                        data += zrle_run_length(length)
                    for xy in order[i:i + length]:
                        pixels[xy] = palette[index]
                    i += length
                assert i == len(order), 'runs must cover the tile'
            else:
                raise ValueError(kind[0])
        if self.shorten:
            data = data[:-self.shorten]
            self.disconnect = END_OF_STREAM
        packed = self.stream.compress(bytes(data))
        out = (u16(self.x) + u16(self.y) + u16(self.w) + u16(self.h)
               + s32(ZRLE) + u32(len(packed)) + packed)
        if self.disconnect is None:
            for (x, y), comps in pixels.items():
                fb.rows[y][x] = comps
        return out


class Case:
    """One replay case: the server's bytes, and what is expected back."""

    def __init__(self, name):
        self.name = name
        self.server = bytearray()
        self.client = bytearray()
        self.events = []
        self.minor = None
        self.fb = None
        self.server_init_offset = None
        self.uses_tight_security = False
        self.tight = None
        self.zrle = None
        # Connecting succeeds at once with the harness's in-memory gates.
        self.event('established')

    def zrle_stream(self):
        """The session's ZRLE zlib stream (see ZrleStream)."""
        if self.zrle is None:
            self.zrle = ZrleStream()
        return self.zrle

    def tight_streams(self):
        """The session's Tight zlib streams (see TightStreams)."""
        if self.tight is None:
            self.tight = TightStreams()
        return self.tight

    def event(self, name, text=None):
        if text is None:
            self.events.append(name)
        else:
            escaped = (text.replace('\\', '\\\\').replace('"', '\\"')
                       .replace('\n', '\\n'))
            self.events.append('%s "%s"' % (name, escaped))

    # Handshake

    def version(self, server_version):
        """The server's ProtocolVersion, and the client's choice."""
        assert len(server_version) == 12
        self.server += server_version
        major = int(server_version[4:7])
        minor = int(server_version[8:11])
        # RemoteViewerCore::handshake(): 3.3, 3.7 or 3.8; anything from
        # 4.0 on is answered with 3.8.
        if major > 3 or minor >= 8:
            self.minor = 8
        elif minor >= 7:
            self.minor = 7
        else:
            self.minor = 3
        self.client += b'RFB 003.%03d\n' % self.minor

    def _offer(self, sec_type):
        """The server offers one security type, and the client takes it."""
        if self.minor >= 7:
            self.server += u8(1) + u8(sec_type)
            self.client += u8(sec_type)
        else:
            self.server += u32(sec_type)

    def security_none(self):
        self._offer(SEC_NONE)
        if self.minor >= 8:
            self.server += u32(0)   # SecurityResult: OK

    def security_vnc(self, password, challenge, ok=True,
                     reason='Authentication failed'):
        """VNC authentication with the harness's password.

        On failure the session ends with an auth-error event; the reason
        string is sent only in RFB 3.8.
        """
        assert len(challenge) == 16
        self._offer(SEC_VNC)
        self.server += challenge
        self.client += vnc_auth_response(password, challenge)
        self.server += u32(0 if ok else 1)
        if not ok:
            if self.minor >= 8:
                self.server += rfb_string(reason)
                self.event('auth-error',
                           'Authentication reason: ' + rfb_text(text_bytes(reason)))
            else:
                self.event('auth-error', 'Authentication failure')

    def security_refused(self, reason):
        """RFB 3.8: the server offers no security types, and says why."""
        assert self.minor >= 8
        self.server += u8(0) + rfb_string(reason)
        self.event('auth-error',
                   'Authentication reason: ' + rfb_text(text_bytes(reason)))

    # Initialisation

    def server_init(self, width, height, pf=RGB888, name='test'):
        """ClientInit and ServerInit, then the client's first requests.

        name is the desktop name, as bytes or as str sent in UTF-8; the
        viewer reports it as a desktop-name event (see rfb_text()).
        """
        self.client += u8(1)   # ClientInit: shared
        self.server_init_offset = len(self.server)
        self.server += u16(width) + u16(height) + pf.pack() + rfb_string(name)
        self.fb = FrameBuffer(width, height, pf)
        self.event('desktop-name', rfb_text(text_bytes(name)))
        self.event('connected')
        self.client += set_encodings(DEFAULT_ENCODINGS)
        self.client += update_request(False, width, height)

    # Server messages

    def update(self, rects):
        """A FramebufferUpdate, and the incremental request that follows.

        If a rectangle makes the viewer stop with an error (its error
        attribute), the session ends there: no request follows, and the
        case should send nothing more.
        """
        self.server += u8(0) + u8(0) + u16(len(rects))
        for rect in rects:
            self.server += rect.encode(self.fb)
            error = getattr(rect, 'error', None)
            if error is not None:
                self.event('error', error)
                return
            disconnect = getattr(rect, 'disconnect', None)
            if disconnect is not None:
                self.event('disconnect', disconnect)
                return
        self.client += update_request(True, self.fb.width, self.fb.height)

    def bell(self):
        self.server += u8(2)
        self.event('bell')

    def server_cut_text(self, text):
        """Plain ServerCutText: bytes as given, or str in Latin-1 as the
        protocol defines it. The viewer reports it as a cut-text event,
        even when empty.
        """
        data = text if isinstance(text, bytes) else text.encode('latin-1')
        self.server += u8(3) + b'\0\0\0' + u32(len(data)) + data
        self.event('cut-text', rfb_text(data))

    def server_cut_text_utf8(self, text):
        """TightVNC's ServerCutTextUtf8 (0xFC000200): str in UTF-8, or
        bytes as given. The viewer reads it whether or not it enabled the
        extension, and ignores it when empty.
        """
        data = text_bytes(text)
        self.server += u32(0xFC000200) + u32(len(data)) + data
        if data:
            self.event('cut-text', data.decode('utf-8'))

    # Output

    def end_of_input(self):
        """The input ends between messages: a normal disconnect."""
        self.event('disconnect', END_OF_STREAM)

    def files(self):
        """The case's files, as {path relative to tests/: contents}."""
        base = os.path.join('cases', self.name)
        files = {
            base + '.rfb': bytes(self.server),
            base + '.out': bytes(self.client),
            base + '.events': ''.join(e + '\n' for e in self.events)
                              .encode('utf-8'),
            base + '.ppm': self.fb.ppm() if self.fb else EMPTY_PPM,
        }
        if self.server_init_offset is not None and not self.uses_tight_security:
            files[os.path.join('fuzz', 'corpus', self.name + '.corpus')] = \
                bytes(self.server[self.server_init_offset:])
        return files


def set_encodings(encodings):
    return (u8(2) + u8(0) + u16(len(encodings))
            + b''.join(s32(e) for e in encodings))


def update_request(incremental, width, height, x=0, y=0):
    return (u8(3) + u8(1 if incremental else 0)
            + u16(x) + u16(y) + u16(width) + u16(height))


# VNC authentication: DES in ECB mode over the 16-byte challenge, keyed
# with the password (at most 8 bytes, padded with zeros) with the bits of
# each key byte reversed, as every VNC implementation does.

def vnc_auth_response(password, challenge):
    key = password.encode('latin-1')[:8].ljust(8, b'\0')
    key = bytes(int('{:08b}'.format(b)[::-1], 2) for b in key)
    subkeys = _des_subkeys(key)
    return _des_block(challenge[:8], subkeys) + _des_block(challenge[8:], subkeys)


_PC1 = [57, 49, 41, 33, 25, 17, 9, 1, 58, 50, 42, 34, 26, 18,
        10, 2, 59, 51, 43, 35, 27, 19, 11, 3, 60, 52, 44, 36,
        63, 55, 47, 39, 31, 23, 15, 7, 62, 54, 46, 38, 30, 22,
        14, 6, 61, 53, 45, 37, 29, 21, 13, 5, 28, 20, 12, 4]
_PC2 = [14, 17, 11, 24, 1, 5, 3, 28, 15, 6, 21, 10,
        23, 19, 12, 4, 26, 8, 16, 7, 27, 20, 13, 2,
        41, 52, 31, 37, 47, 55, 30, 40, 51, 45, 33, 48,
        44, 49, 39, 56, 34, 53, 46, 42, 50, 36, 29, 32]
_SHIFTS = [1, 1, 2, 2, 2, 2, 2, 2, 1, 2, 2, 2, 2, 2, 2, 1]
_IP = [58, 50, 42, 34, 26, 18, 10, 2, 60, 52, 44, 36, 28, 20, 12, 4,
       62, 54, 46, 38, 30, 22, 14, 6, 64, 56, 48, 40, 32, 24, 16, 8,
       57, 49, 41, 33, 25, 17, 9, 1, 59, 51, 43, 35, 27, 19, 11, 3,
       61, 53, 45, 37, 29, 21, 13, 5, 63, 55, 47, 39, 31, 23, 15, 7]
_FP = [40, 8, 48, 16, 56, 24, 64, 32, 39, 7, 47, 15, 55, 23, 63, 31,
       38, 6, 46, 14, 54, 22, 62, 30, 37, 5, 45, 13, 53, 21, 61, 29,
       36, 4, 44, 12, 52, 20, 60, 28, 35, 3, 43, 11, 51, 19, 59, 27,
       34, 2, 42, 10, 50, 18, 58, 26, 33, 1, 41, 9, 49, 17, 57, 25]
_E = [32, 1, 2, 3, 4, 5, 4, 5, 6, 7, 8, 9, 8, 9, 10, 11, 12, 13,
      12, 13, 14, 15, 16, 17, 16, 17, 18, 19, 20, 21, 20, 21, 22, 23, 24, 25,
      24, 25, 26, 27, 28, 29, 28, 29, 30, 31, 32, 1]
_P = [16, 7, 20, 21, 29, 12, 28, 17, 1, 15, 23, 26, 5, 18, 31, 10,
      2, 8, 24, 14, 32, 27, 3, 9, 19, 13, 30, 6, 22, 11, 4, 25]
_SBOX = [
    [14, 4, 13, 1, 2, 15, 11, 8, 3, 10, 6, 12, 5, 9, 0, 7,
     0, 15, 7, 4, 14, 2, 13, 1, 10, 6, 12, 11, 9, 5, 3, 8,
     4, 1, 14, 8, 13, 6, 2, 11, 15, 12, 9, 7, 3, 10, 5, 0,
     15, 12, 8, 2, 4, 9, 1, 7, 5, 11, 3, 14, 10, 0, 6, 13],
    [15, 1, 8, 14, 6, 11, 3, 4, 9, 7, 2, 13, 12, 0, 5, 10,
     3, 13, 4, 7, 15, 2, 8, 14, 12, 0, 1, 10, 6, 9, 11, 5,
     0, 14, 7, 11, 10, 4, 13, 1, 5, 8, 12, 6, 9, 3, 2, 15,
     13, 8, 10, 1, 3, 15, 4, 2, 11, 6, 7, 12, 0, 5, 14, 9],
    [10, 0, 9, 14, 6, 3, 15, 5, 1, 13, 12, 7, 11, 4, 2, 8,
     13, 7, 0, 9, 3, 4, 6, 10, 2, 8, 5, 14, 12, 11, 15, 1,
     13, 6, 4, 9, 8, 15, 3, 0, 11, 1, 2, 12, 5, 10, 14, 7,
     1, 10, 13, 0, 6, 9, 8, 7, 4, 15, 14, 3, 11, 5, 2, 12],
    [7, 13, 14, 3, 0, 6, 9, 10, 1, 2, 8, 5, 11, 12, 4, 15,
     13, 8, 11, 5, 6, 15, 0, 3, 4, 7, 2, 12, 1, 10, 14, 9,
     10, 6, 9, 0, 12, 11, 7, 13, 15, 1, 3, 14, 5, 2, 8, 4,
     3, 15, 0, 6, 10, 1, 13, 8, 9, 4, 5, 11, 12, 7, 2, 14],
    [2, 12, 4, 1, 7, 10, 11, 6, 8, 5, 3, 15, 13, 0, 14, 9,
     14, 11, 2, 12, 4, 7, 13, 1, 5, 0, 15, 10, 3, 9, 8, 6,
     4, 2, 1, 11, 10, 13, 7, 8, 15, 9, 12, 5, 6, 3, 0, 14,
     11, 8, 12, 7, 1, 14, 2, 13, 6, 15, 0, 9, 10, 4, 5, 3],
    [12, 1, 10, 15, 9, 2, 6, 8, 0, 13, 3, 4, 14, 7, 5, 11,
     10, 15, 4, 2, 7, 12, 9, 5, 6, 1, 13, 14, 0, 11, 3, 8,
     9, 14, 15, 5, 2, 8, 12, 3, 7, 0, 4, 10, 1, 13, 11, 6,
     4, 3, 2, 12, 9, 5, 15, 10, 11, 14, 1, 7, 6, 0, 8, 13],
    [4, 11, 2, 14, 15, 0, 8, 13, 3, 12, 9, 7, 5, 10, 6, 1,
     13, 0, 11, 7, 4, 9, 1, 10, 14, 3, 5, 12, 2, 15, 8, 6,
     1, 4, 11, 13, 12, 3, 7, 14, 10, 15, 6, 8, 0, 5, 9, 2,
     6, 11, 13, 8, 1, 4, 10, 7, 9, 5, 0, 15, 14, 2, 3, 12],
    [13, 2, 8, 4, 6, 15, 11, 1, 10, 9, 3, 14, 5, 0, 12, 7,
     1, 15, 13, 8, 10, 3, 7, 4, 12, 5, 6, 11, 0, 14, 9, 2,
     7, 11, 4, 1, 9, 12, 14, 2, 0, 6, 10, 13, 15, 3, 5, 8,
     2, 1, 14, 7, 4, 10, 8, 13, 15, 12, 9, 0, 3, 5, 6, 11],
]


def _permute(value, table, in_bits):
    out = 0
    for pos in table:
        out = (out << 1) | ((value >> (in_bits - pos)) & 1)
    return out


def _des_subkeys(key):
    k = _permute(int.from_bytes(key, 'big'), _PC1, 64)
    c, d = k >> 28, k & 0xFFFFFFF
    subkeys = []
    for shift in _SHIFTS:
        c = ((c << shift) | (c >> (28 - shift))) & 0xFFFFFFF
        d = ((d << shift) | (d >> (28 - shift))) & 0xFFFFFFF
        subkeys.append(_permute((c << 28) | d, _PC2, 56))
    return subkeys


def _des_block(block, subkeys):
    v = _permute(int.from_bytes(block, 'big'), _IP, 64)
    left, right = v >> 32, v & 0xFFFFFFFF
    for subkey in subkeys:
        x = _permute(right, _E, 32) ^ subkey
        s = 0
        for i in range(8):
            six = (x >> (42 - 6 * i)) & 0x3F
            row = ((six >> 4) & 2) | (six & 1)
            s = (s << 4) | _SBOX[i][row * 16 + ((six >> 1) & 0xF)]
        left, right = right, left ^ _permute(s, _P, 32)
    return _permute((right << 32) | left, _FP, 64).to_bytes(8, 'big')
