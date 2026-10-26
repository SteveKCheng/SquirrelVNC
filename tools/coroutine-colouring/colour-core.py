# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

# Colours RemoteViewerCore's read path (notes/async-networking.md, Phase 2,
# last layer). Usage: colour-core.py DIR (holding RemoteViewerCore.h/.cpp).
# execute() is split by hand afterwards.
import re, sys, os
d = sys.argv[1]
names = ['handshake', 'negotiateSecurityType', 'readSecurityTypeList',
         'initTunnelling', 'initAuthentication', 'authenticate',
         'readCapabilities', 'readCapability', 'clientAndServerInit',
         'readPixelFormat', 'receiveServerMessageType', 'receiveFbUpdate',
         'receiveFbUpdateRectangle', 'receiveSetColorMapEntries',
         'receiveServerCutText', 'receiveServerCutTextUtf8', 'skipBytes',
         'readReason', 'processPseudoEncoding']
def rd(p): return open(p, newline='').read().replace('\r\n', '\n')
def wr(p, s): open(p, 'w', newline='').write(s.replace('\n', '\r\n'))
def task(t):
    t = t.strip()
    if t == 'Task<>': return t
    return 'Task<>' if t == 'void' else 'Task<' + t + '>'
alt = '|'.join(names)
# --- source
p = os.path.join(d, 'RemoteViewerCore.cpp'); s = rd(p)
s = s.replace('AwaitableReader &reader, const Rect &rect', 'const Rect &rect')
s = s.replace('Task<> RemoteViewerCore::processPseudoEncoding(AwaitableReader &reader,\n                                               const Rect &rect,\n',
              'Task<> RemoteViewerCore::processPseudoEncoding(const Rect &rect,\n@@REMOVE@@\n')
found = set()
def defn(m):
    found.add(m.group(2))
    return task(m.group(1)) + ' RemoteViewerCore::' + m.group(2) + '('
s = re.sub(r'^([A-Za-z][\w:<> ]*?)\s+RemoteViewerCore::(' + alt + r')\(', defn, s, flags=re.M)
assert found == set(names), set(names) - found
out = []
for line in s.split('\n'):
    if re.match(r'^Task<.*?> RemoteViewerCore::', line):
        out.append(line); continue
    code, sep, comment = line.partition('//')
    code = re.sub(r'\bm_input->read', 'co_await m_reader->read', code)
    code = re.sub(r'(?<![\w.>:])(' + alt + r')\(', r'co_await \1(', code)
    out.append(code + sep + comment)
s = '\n'.join(out)
# existing bridges and per-call readers
s = re.sub(r'^[ ]*AwaitableReader reader\(\*m_input\);$', '@@REMOVE@@', s, flags=re.M)
i = s.index('syncWait(rectangleDecoder->process(reader,')
j = s.index('));', i)
s = s[:i] + 'co_await rectangleDecoder->process(*m_reader,' + s[i + len('syncWait(rectangleDecoder->process(reader,'):j] + ');' + s[j + 3:]
s = s.replace('co_await reader.readFully(', 'co_await m_reader->readFully(')
s = s.replace('syncWait(co_await processPseudoEncoding(reader, rect, encodingType));', 'co_await processPseudoEncoding(rect, encodingType);')
s = s.replace('syncWait(m_serverMsgHandlers[msgType]->onServerMessage(msgType, reader));', 'co_await m_serverMsgHandlers[msgType]->onServerMessage(msgType, *m_reader);')
s = s.replace('syncWait(m_authHandlers[authenticationType]->authenticate(reader, m_output));', 'co_await m_authHandlers[authenticationType]->authenticate(*m_reader, m_output);')
assert 'syncWait' not in s or s.count('syncWait') == 0, 'leftover syncWait'
# return -> co_return within coloured definitions
res, pos = [], 0
for m in re.finditer(r'^Task<.*?> RemoteViewerCore::(?:' + alt + r')\(', s, re.M):
    end = s.index('\n}\n', m.end()) + 3
    res.append(s[pos:m.start()])
    body = []
    for line in s[m.start():end].split('\n'):
        code, sep, comment = line.partition('//')
        body.append(re.sub(r'\breturn\b', 'co_return', code) + sep + comment)
    res.append('\n'.join(body))
    pos = end
res.append(s[pos:]); s = ''.join(res)
wr(p, s)
# --- header
p = os.path.join(d, 'RemoteViewerCore.h'); h = rd(p)
h = h.replace('Task<> processPseudoEncoding(AwaitableReader &reader, const Rect &rect, int encType);',
              'Task<> processPseudoEncoding(const Rect &rect, int encType);')
hf = set()
def hdecl(m):
    hf.add(m.group(3))
    return m.group(1) + task(m.group(2)) + ' ' + m.group(3) + '('
h = re.sub(r'^(\s+)((?:const\s+)?[A-Za-z][\w:<>]*?)\s+(' + alt + r')\(', hdecl, h, flags=re.M)
assert hf == set(names), set(names) - hf
wr(p, h)
print('ok')
