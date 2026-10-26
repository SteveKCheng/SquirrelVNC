# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

# Moves the body of the try block of RemoteViewerCore::execute() into the
# coroutine run(), which execute() runs with syncWait(); declares run() and
# m_reader. Usage: split-execute.py DIR.
import os, sys
d = sys.argv[1]
def rd(p): return open(p, newline='').read().replace('\r\n', '\n')
def wr(p, s): open(p, 'w', newline='').write(s.replace('\n', '\r\n'))
p = os.path.join(d, 'RemoteViewerCore.cpp'); s = rd(p)
head = 'void RemoteViewerCore::execute()\n{\n  try {\n'
a = s.index(head); b0 = a + len(head)
b1 = s.index('  } catch (const AuthException &ex) {', b0)
body = '\n'.join(l[2:] if l.startswith('  ') else l for l in s[b0:b1].split('\n'))
assert body.count('  connectToHost();\n') == 1
body = body.replace('  connectToHost();\n', '  connectToHost();\n  m_reader.emplace(*m_input);\n')
run = 'Task<> RemoteViewerCore::run()\n{\n' + body + '}\n\n'
call = ('void RemoteViewerCore::execute()\n{\n  try {\n'
        '    // The protocol, a coroutine that never suspends yet: its reads are\n'
        '    // always ready (notes/async-networking.md, Phase 2).\n'
        '    syncWait(run());\n')
s = s[:a] + run + call + s[b1:]
wr(p, s)
p = os.path.join(d, 'RemoteViewerCore.h'); h = rd(p)
o = '  // Overrides Thread::execute(). Implements the input thread.\n  //\n  void execute() override;\n'
assert h.count(o) == 1
h = h.replace(o, o + '''
  //
  // The protocol, from connecting to the end of the session, as a coroutine
  // (util/Task.h) that reads through m_reader; execute() runs it with
  // syncWait(), and reports how it ended to the adapter.
  //
  // Post-conditions: when it returns, the session has ended normally, and
  // the adapter has been told with onDisconnect().
  // Throws what the protocol throws: AuthException, IOException when the
  // connection fails, or Exception.
  //
  Task<> run();
''')
o2 = '  RfbInputGate *m_input;\n'
assert h.count(o2) == 1
h = h.replace(o2, o2 + '''  // Reads m_input for the coroutines of the protocol (run() and those it
  // awaits). Made by run() once the connection is established; used only on
  // the core thread.
  std::optional<AwaitableReader> m_reader;
''')
assert h.count('#include <mutex>\n') == 1
h = h.replace('#include <mutex>\n', '#include <mutex>\n#include <optional>\n')
wr(p, h)
print('split')
