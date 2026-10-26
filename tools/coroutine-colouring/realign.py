# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

# Re-aligns continuation lines after a line that colour.py changed: lines
# aligned one past an unclosed '(' of the old line move with that '('.
# Usage: realign.py OLD NEW (NEW is rewritten). Both files have the same
# number of lines.
import sys
def rd(p): return open(p, newline='').read().replace('\r\n', '\n').split('\n')
old, new = rd(sys.argv[1]), rd(sys.argv[2])
assert len(old) == len(new), 'line count changed'
def open_paren_col(line):
    depth, stack = 0, []
    for i, ch in enumerate(line):
        if ch == '(':
            stack.append(i)
        elif ch == ')' and stack:
            stack.pop()
    return stack[-1] if stack else None
i = 0
while i < len(new):
    if old[i] != new[i]:
        oc, nc = open_paren_col(old[i]), open_paren_col(new[i])
        if oc is not None and nc is not None and oc != nc:
            j = i + 1
            while j < len(new) and old[j] == new[j] and old[j].startswith(' ' * (oc + 1)) and not old[j][oc + 1:oc + 2].isspace():
                new[j] = ' ' * (nc + 1) + old[j][oc + 1:]
                j += 1
    i += 1
open(sys.argv[2], 'w', newline='').write('\r\n'.join(new))
