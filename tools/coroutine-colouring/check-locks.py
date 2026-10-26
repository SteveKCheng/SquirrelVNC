# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

"""Lists each co_await inside a scope that holds a lock.

Usage: check-locks.py FILE...

For every function defined as "Task<...> Class::name(" in the files, it
follows the braces and reports a line with co_await that comes after an
AutoLock, std::lock_guard or std::unique_lock in an enclosing scope. No
lock may be held across a co_await (notes/async-networking.md, Phase 3).
A heuristic: it reads braces and "//" comments only, not strings.
Prints the number of coroutines checked; exits with 1 if it found any.
"""
import re
import sys

found = 0
checked = 0
for path in sys.argv[1:]:
    s = open(path, newline='', errors='replace').read().replace('\r\n', '\n')
    for m in re.finditer(r'^Task<.*?> \w+::(\w+)\(', s, re.M):
        checked += 1
        end = s.index('\n}\n', m.end())
        depth = 0
        locks = []
        for line in s[m.end():end].split('\n'):
            code = line.split('//')[0]
            if re.search(r'\b(AutoLock|lock_guard|unique_lock)\b', code):
                locks.append(depth)
            if 'co_await' in code and locks:
                print('%s: %s: %s' % (path, m.group(1), line.strip()))
                found += 1
            depth += code.count('{') - code.count('}')
            while locks and depth < locks[-1]:
                locks.pop()
print('%d coroutines checked' % checked)
sys.exit(1 if found else 0)
