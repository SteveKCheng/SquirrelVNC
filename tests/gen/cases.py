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

"""The RFB replay test cases.

Each function decorated with @case builds one case. Running this script
writes every case's files under tests/; with --check, it writes nothing
and reports files that differ from what it would write, are missing, or
are left over from a case that no longer exists.

    python3 tests/gen/cases.py [--check]
"""

import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from rfbgen import (RGB565, Case, HextileRect, HextileTile, Image,   # noqa: E402
                    RawRect, RreRect, TightRect, load_ppm)

CASES = []

# The password rfb-replay gives when asked, unless told otherwise.
HARNESS_PASSWORD = 'password'

CHALLENGE = bytes(range(0x10, 0x20))


def case(function):
    CASES.append(function)
    return function


def raw_session(c, width, height):
    """ServerInit, then the whole screen in Raw."""
    c.server_init(width, height)
    c.update([RawRect(0, 0, width, height, Image.pattern(width, height))])


@case
def raw_rgb888():
    """The minimal session: RFB 3.8, no security, Raw at 32 bpp.

    Several updates, partial rectangles at offsets, an empty update, a
    bell, and strips along the bottom and right edges.
    """
    c = Case('raw-rgb888')
    c.version(b'RFB 003.008\n')
    c.security_none()
    raw_session(c, 32, 24)
    second = Image.pattern(32, 24, seed=1)
    c.update([RawRect(5, 3, 10, 6, second),
              RawRect(20, 10, 12, 14, second)])
    c.update([])
    c.bell()
    c.update([RawRect(0, 23, 32, 1, second), RawRect(31, 0, 1, 23, second)])
    c.end_of_input()
    return c


@case
def version_33():
    """RFB 3.3: the server chooses the security type, with no result."""
    c = Case('version-3.3')
    c.version(b'RFB 003.003\n')
    c.security_none()
    raw_session(c, 8, 8)
    c.end_of_input()
    return c


@case
def version_37():
    """RFB 3.7: a security type list, but no result for None."""
    c = Case('version-3.7')
    c.version(b'RFB 003.007\n')
    c.security_none()
    raw_session(c, 8, 8)
    c.end_of_input()
    return c


@case
def version_35():
    """RFB 3.5, which some old servers send, is answered with 3.3."""
    c = Case('version-3.5')
    c.version(b'RFB 003.005\n')
    c.security_none()
    raw_session(c, 8, 8)
    c.end_of_input()
    return c


@case
def version_4():
    """A version from 4.0 on is answered with 3.8."""
    c = Case('version-4.1')
    c.version(b'RFB 004.001\n')
    c.security_none()
    raw_session(c, 8, 8)
    c.end_of_input()
    return c


@case
def vnc_auth():
    """VNC authentication that succeeds."""
    c = Case('vnc-auth')
    c.version(b'RFB 003.008\n')
    c.security_vnc(HARNESS_PASSWORD, CHALLENGE)
    raw_session(c, 8, 8)
    c.end_of_input()
    return c


@case
def vnc_auth_failed():
    """VNC authentication that fails, with a reason (RFB 3.8)."""
    c = Case('vnc-auth-failed')
    c.version(b'RFB 003.008\n')
    c.security_vnc(HARNESS_PASSWORD, CHALLENGE, ok=False,
                   reason='Wrong password')
    return c


@case
def vnc_auth_failed_37():
    """VNC authentication that fails in RFB 3.7, which sends no reason."""
    c = Case('vnc-auth-failed-3.7')
    c.version(b'RFB 003.007\n')
    c.security_vnc(HARNESS_PASSWORD, CHALLENGE, ok=False)
    return c


@case
def security_refused():
    """RFB 3.8: no security types offered, with a reason."""
    c = Case('security-refused')
    c.version(b'RFB 003.008\n')
    c.security_refused('Too many connections')
    return c


RED = (255, 0, 0)
GREEN = (0, 255, 0)
BLUE = (0, 0, 255)
GREY = (128, 128, 128)


@case
def rre():
    """RRE: a background and subrectangles, some on the rectangle's edges."""
    c = Case('rre')
    c.version(b'RFB 003.008\n')
    c.security_none()
    raw_session(c, 32, 24)
    c.update([RreRect(4, 2, 20, 16, GREY,
                      [(0, 0, 5, 3, RED),
                       (15, 13, 5, 3, GREEN),
                       (8, 6, 1, 1, BLUE),
                       (0, 15, 20, 1, BLUE)])])
    c.update([RreRect(24, 18, 8, 6, GREEN)])
    c.end_of_input()
    return c


@case
def rre_empty_at_bottom_edge():
    """An empty RRE rectangle lying on the frame buffer's bottom edge.

    The protocol allows empty rectangles, and the viewer's bounds check
    accepts this one. Filling its background must change nothing:
    FrameBuffer::fillRect() used to write its first row, which starts at
    the end of the buffer, past the frame buffer.
    """
    c = Case('rre-empty-at-bottom-edge')
    c.version(b'RFB 003.008\n')
    c.security_none()
    raw_session(c, 8, 8)
    c.update([RreRect(0, 8, 8, 0, RED)])
    c.update([RreRect(2, 2, 4, 4, BLUE)])
    c.end_of_input()
    return c


@case
def rre_subrect_outside():
    """An RRE subrectangle outside its rectangle ends the session.

    The frame buffer keeps what the update before it drew: nothing of the
    rejected rectangle is copied to it.
    """
    c = Case('rre-subrect-outside')
    c.version(b'RFB 003.008\n')
    c.security_none()
    raw_session(c, 8, 8)
    c.update([RreRect(0, 0, 8, 8, GREY,
                      [(1, 1, 2, 2, RED),
                       (0, 8, 8, 1, BLUE)])])
    return c


@case
def hextile():
    """Hextile: raw tiles, backgrounds carried over, coloured and
    foreground subrectangles, and smaller tiles at the right and bottom
    edges of a 20 x 20 rectangle.
    """
    c = Case('hextile')
    c.version(b'RFB 003.008\n')
    c.security_none()
    raw_session(c, 32, 24)
    second = Image.pattern(32, 24, seed=2)
    c.update([HextileRect(2, 2, 20, 20, [
        # 16 x 16: background, foreground subrectangles.
        HextileTile(background=GREY, foreground=RED,
                    subrects=[(0, 0, 16, 1, None), (3, 4, 2, 5, None),
                              (15, 15, 1, 1, None)]),
        # 4 x 16, at the right edge: raw.
        HextileTile(raw=second),
        # 16 x 4, at the bottom edge: coloured subrectangles.
        HextileTile(background=BLUE, coloured=True,
                    subrects=[(0, 0, 4, 4, GREEN), (12, 1, 4, 3, RED)]),
        # 4 x 4: background carried over, new foreground.
        HextileTile(foreground=GREEN, subrects=[(1, 1, 3, 3, None)]),
    ])])
    c.end_of_input()
    return c


@case
def hextile_subrect_outside_tile():
    """A Hextile subrectangle outside its tile ends the session.

    The second tile of a 20 x 8 rectangle is 4 pixels wide, so a
    subrectangle from x = 2 that is 3 wide does not fit, although it
    would fit in a full 16 x 16 tile. Nothing of the rectangle reaches
    the frame buffer.
    """
    c = Case('hextile-subrect-outside-tile')
    c.version(b'RFB 003.008\n')
    c.security_none()
    raw_session(c, 32, 24)
    c.update([HextileRect(0, 0, 20, 8, [
        HextileTile(background=GREY),
        HextileTile(foreground=RED, subrects=[(2, 0, 3, 1, None)]),
    ])])
    return c


def checker(w, h, n=2, size=1):
    """Palette indices for a w x h rectangle: n colours in squares."""
    return [[((x // size) + (y // size)) % n for x in range(w)] for y in range(h)]


def tight_session(name, width=32, height=24, pf=None):
    c = Case(name)
    c.version(b'RFB 003.008\n')
    c.security_none()
    if pf is None:
        raw_session(c, width, height)
    else:
        c.server_init(width, height, pf)
        c.update([RawRect(0, 0, width, height, Image.pattern(width, height))])
    return c


@case
def tight_fill_and_copy():
    """Tight fill, and the copy filter: below 12 bytes (sent as is) and
    compressed, with two rectangles in turn on one zlib stream, whose
    state must carry on, and one on another stream.
    """
    c = tight_session('tight-fill-and-copy')
    z = c.tight_streams()
    image = Image.pattern(32, 24, seed=3)
    c.update([TightRect(0, 0, 32, 4, ('fill', GREY), z),
              TightRect(1, 5, 3, 1, ('copy', image), z),
              TightRect(2, 7, 16, 8, ('copy', image), z, stream=0),
              TightRect(18, 7, 12, 8, ('copy', image), z, stream=0)])
    c.update([TightRect(4, 16, 20, 6, ('copy', image), z, stream=1),
              TightRect(24, 0, 8, 4, ('fill', BLUE), z)])
    c.end_of_input()
    return c


@case
def tight_palette():
    """Tight palette filter: two colours, packed one bit per pixel in
    rows of whole bytes (13 is not a multiple of 8), and five colours,
    one byte per pixel; each small enough to be sent as is, and large
    enough to be compressed.
    """
    c = tight_session('tight-palette')
    z = c.tight_streams()
    two = [RED, BLUE]
    five = [RED, GREEN, BLUE, GREY, (255, 255, 255)]
    c.update([TightRect(0, 0, 13, 2, ('palette', two, checker(13, 2)), z),
              TightRect(0, 3, 13, 9, ('palette', two, checker(13, 9, 2, 2)), z,
                        stream=2),
              TightRect(14, 0, 3, 3, ('palette', five, checker(3, 3, 5)), z),
              TightRect(14, 4, 18, 18, ('palette', five, checker(18, 18, 5, 3)), z,
                        stream=2)])
    c.end_of_input()
    return c


@case
def tight_gradient():
    """Tight gradient filter, which sends each colour component as its
    difference from a prediction from the neighbouring pixels.
    """
    c = tight_session('tight-gradient')
    z = c.tight_streams()
    image = Image.pattern(32, 24, seed=4)
    c.update([TightRect(3, 2, 25, 19, ('gradient', image), z, stream=3),
              TightRect(0, 22, 2, 1, ('gradient', image), z)])
    c.end_of_input()
    return c


@case
def tight_stream_reset():
    """A Tight rectangle that resets its zlib stream: the viewer must
    start a new inflater, or the new stream's header would not decode.
    """
    c = tight_session('tight-stream-reset')
    z = c.tight_streams()
    image = Image.pattern(32, 24, seed=5)
    c.update([TightRect(0, 0, 16, 12, ('copy', image), z, stream=1)])
    c.update([TightRect(16, 0, 16, 12, ('copy', image), z, stream=1, reset=0b0010),
              TightRect(0, 12, 32, 12, ('copy', image), z, stream=1)])
    c.end_of_input()
    return c


@case
def tight_rgb565():
    """Tight at 16 bits per pixel, where pixels are sent whole rather
    than as 3 bytes: fill, copy, palette and gradient.
    """
    c = tight_session('tight-rgb565', pf=RGB565)
    z = c.tight_streams()
    image = Image.pattern(32, 24, seed=6)
    c.update([TightRect(0, 0, 8, 8, ('fill', GREEN), z),
              TightRect(8, 0, 16, 8, ('copy', image), z),
              TightRect(0, 8, 16, 16, ('palette', [RED, BLUE, GREY],
                                       checker(16, 16, 3, 4)), z, stream=1),
              TightRect(16, 8, 16, 16, ('gradient', image), z, stream=2)])
    c.end_of_input()
    return c


@case
def tight_bad_compression_type():
    """A Tight compression type above 9 ends the session with an error."""
    c = tight_session('tight-bad-compression-type')
    z = c.tight_streams()
    c.update([TightRect(0, 0, 8, 8, ('fill', RED), z, compression=0xA)])
    return c


@case
def tight_short_data():
    """Tight data that inflates to fewer bytes than the rectangle needs
    ends the session with an error, and draws nothing.
    """
    c = tight_session('tight-short-data')
    z = c.tight_streams()
    image = Image.pattern(32, 24, seed=7)
    c.update([TightRect(0, 0, 16, 8, ('copy', image), z, shorten=3)])
    return c


JPEG_DIR = os.path.join(os.path.dirname(os.path.abspath(__file__)), 'jpeg')


def quadrants_jpeg():
    """The test JPEG, and the image the viewer's decoder makes of it.

    quadrants-16x16.jpg is four solid 8 x 8 quadrants, (200, 40, 40),
    (40, 160, 60), (50, 70, 190) and (230, 210, 60) in reading order,
    written once by libjpeg-turbo 3.2.0's tj3Compress8() at quality 95
    without chroma subsampling. Solid blocks leave little room for
    decoders to differ. quadrants-16x16.ppm is what rfb-replay decoded,
    reviewed by eye; the generator has no JPEG codec of its own.
    """
    data = open(os.path.join(JPEG_DIR, 'quadrants-16x16.jpg'), 'rb').read()
    decoded_path = os.path.join(JPEG_DIR, 'quadrants-16x16.ppm')
    if os.path.exists(decoded_path):
        decoded = load_ppm(decoded_path)
    else:
        # Before the first run: the colours as encoded.
        colours = [(200, 40, 40), (40, 160, 60), (50, 70, 190), (230, 210, 60)]
        decoded = Image(16, 16)
        for y in range(16):
            for x in range(16):
                decoded.put(x, y, colours[(y // 8) * 2 + x // 8])
    return data, decoded


@case
def tight_jpeg():
    """Tight JPEG: two rectangles, the second overlapping the first."""
    data, decoded = quadrants_jpeg()
    c = tight_session('tight-jpeg')
    z = c.tight_streams()
    c.update([TightRect(4, 4, 16, 16, ('jpeg', data, decoded), z),
              TightRect(14, 6, 16, 16, ('jpeg', data, decoded), z)])
    c.end_of_input()
    return c


@case
def tight_jpeg_rgb565():
    """Tight JPEG at 16 bits per pixel, converted to RGB565 by the viewer."""
    data, decoded = quadrants_jpeg()
    c = tight_session('tight-jpeg-rgb565', pf=RGB565)
    z = c.tight_streams()
    c.update([TightRect(8, 4, 16, 16, ('jpeg', data, decoded), z)])
    c.end_of_input()
    return c


@case
def tight_jpeg_wrong_size():
    """A JPEG whose size differs from its rectangle's is ignored: the
    viewer logs it, leaves the rectangle as it was, and goes on.
    """
    data, _ = quadrants_jpeg()
    c = tight_session('tight-jpeg-wrong-size')
    z = c.tight_streams()
    c.update([TightRect(0, 0, 16, 8, ('jpeg', data, None), z)])
    c.update([TightRect(16, 8, 8, 8, ('fill', RED), z)])
    c.end_of_input()
    return c


@case
def text_ascii_and_utf8():
    """Text from the server that today's viewer already decodes as
    notes/string-encoding.md decides: an ASCII desktop name, plain cut
    text in ASCII (empty included), and TightVNC UTF-8 cut text outside
    ASCII, beyond the Basic Multilingual Plane too. Quotes, backslashes
    and newlines check the event log's escaping.
    """
    c = Case('text-ascii-and-utf8')
    c.version(b'RFB 003.008\n')
    c.security_none()
    c.server_init(8, 8, name='Test desktop (ASCII) "#1"')
    c.server_cut_text('Plain text, "quoted" \\ back\nslashed')
    c.server_cut_text('')
    c.server_cut_text_utf8('Gr\u00fc\u00dfe, \u65e5\u672c\u8a9e, \U0001f43f\ufe0f')
    c.server_cut_text_utf8('')
    c.bell()
    c.end_of_input()
    return c


@case
def vnc_auth_failed_utf8_reason():
    """A 3.8 authentication failure whose reason is UTF-8 outside ASCII,
    which the viewer already reads as UTF-8.
    """
    c = Case('vnc-auth-failed-utf8-reason')
    c.version(b'RFB 003.008\n')
    c.security_vnc(HARNESS_PASSWORD, CHALLENGE, ok=False,
                   reason='Mot de passe incorrect \u2014 r\u00e9essayez')
    return c


def generate():
    """All files of all cases, as {path relative to tests/: bytes}."""
    files = {}
    for function in CASES:
        files.update(function().files())
    return files


def generated_dirs():
    return [os.path.join('cases'), os.path.join('fuzz', 'corpus')]


def main(argv):
    check = argv[1:] == ['--check']
    if argv[1:] and not check:
        print(__doc__.strip().splitlines()[-1].strip())
        return 2
    tests_dir = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
    files = generate()

    existing = set()
    for d in generated_dirs():
        full = os.path.join(tests_dir, d)
        if os.path.isdir(full):
            for name in os.listdir(full):
                existing.add(os.path.join(d, name))
    stale = sorted(existing - set(files))

    if check:
        problems = 0
        for path, data in sorted(files.items()):
            full = os.path.join(tests_dir, path)
            if not os.path.exists(full):
                print('missing:', path)
                problems += 1
            elif open(full, 'rb').read() != data:
                print('differs:', path)
                problems += 1
        for path in stale:
            print('stale:  ', path)
            problems += 1
        return 1 if problems else 0

    for path, data in files.items():
        full = os.path.join(tests_dir, path)
        os.makedirs(os.path.dirname(full), exist_ok=True)
        with open(full, 'wb') as f:
            f.write(data)
    for path in stale:
        os.remove(os.path.join(tests_dir, path))
        print('removed:', path)
    print('wrote %d files for %d cases' % (len(files), len(CASES)))
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv))
