# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

# Deletes the lines colour-core.py marked "@@REMOVE@@", with a blank line
# after one when a blank line also precedes it. Usage: dropmarked.py FILE.
import sys
p = sys.argv[1]
lines = open(p, newline='').read().replace('\r\n', '\n').split('\n')
out = []
i = 0
while i < len(lines):
    if lines[i].strip() == '@@REMOVE@@':
        if out and out[-1].strip() == '' and i + 1 < len(lines) and lines[i + 1].strip() == '':
            i += 1
        i += 1
        continue
    out.append(lines[i]); i += 1
open(p, 'w', newline='').write('\r\n'.join(out))
