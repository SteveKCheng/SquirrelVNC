# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

# Colours the read path of the decoders: see the plan in
# notes/async-networking.md, Phase 2. Usage: colour.py DIR (viewer-core).
import re, sys, os
d = sys.argv[1]
# function name -> (old return type, new return type), per file stem
funcs = {
  'DecoderOfRectangle': {'process': ('void', 'Task<>'), 'decode': ('void', 'Task<>')},
  'RawDecoder': {'process': ('void', 'Task<>'), 'decode': ('void', 'Task<>')},
  'CopyRectDecoder': {'decode': ('void', 'Task<>')},
  'RreDecoder': {'decode': ('void', 'Task<>')},
  'HexTileDecoder': {'decode': ('void', 'Task<>')},
  'ZrleDecoder': {'decode': ('void', 'Task<>'), 'readAndInflate': ('void', 'Task<>')},
  'TightDecoder': {
    'decode': ('void', 'Task<>'),
    'readTightPixel': ('UINT32', 'Task<UINT32>'),
    'readCompactSize': ('int', 'Task<int>'),
    'readPalette': ('std::vector<UINT32>', 'Task<std::vector<UINT32>>'),
    'processJpeg': ('void', 'Task<>'),
    'processBasicTypes': ('void', 'Task<>'),
    'readTightData': ('void', 'Task<>'),
    'readCompressedData': ('void', 'Task<>'),
  },
}
def rd(p): return open(p, newline='').read().replace('\r\n', '\n')
def wr(p, s): open(p, 'w', newline='').write(s.replace('\n', '\r\n'))
for stem, fs in funcs.items():
    names = '|'.join(fs)
    found = {name: 0 for name in fs}
    for ext in ('.h', '.cpp'):
        p = os.path.join(d, stem + ext)
        s = rd(p)
        # signatures: return types
        for name, (old, new) in fs.items():
            pat = re.compile(r'^(\s*(?:virtual\s+)?)' + re.escape(old) + r'(\s+(?:\w+::)?' + name + r'\(\s*RfbInputGate &input)', re.M)
            s, n = pat.subn(lambda m: m.group(1) + new + m.group(2), s)
            found[name] += n
        s = s.replace('RfbInputGate &input', 'AwaitableReader &reader')
        if ext == '.cpp':
            # reads, and calls to coloured functions
            s = re.sub(r'\binput\.read', 'co_await reader.read', s)
            s = re.sub(r'((?:\w+::)?\b(?:' + names + r'))\(input\b', r'co_await \1(reader', s)
            assert not re.search(r'\binput\.', s), p
            # return -> co_return inside coloured definitions
            out, pos = [], 0
            for m in re.finditer(r'^(?:Task<[^\n]*?>)\s+\w+::(?:' + names + r')\(', s, re.M):
                end = s.index('\n}\n', m.end()) + 3
                out.append(s[pos:m.start()])
                out.append(re.sub(r'\breturn\b', 'co_return', s[m.start():end]))
                pos = end
            out.append(s[pos:])
            s = ''.join(out)
        wr(p, s)
        print('coloured', p)
    assert all(found.values()), (stem, found)
