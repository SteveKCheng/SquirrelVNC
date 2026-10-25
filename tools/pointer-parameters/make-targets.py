# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

"""Step 7: write targets.json for apply-rewrite.py.

Usage: make-targets.py WORK/grp.json SELECTION.json TARGETS.json
SELECTION is a JSON list of [file stem, function, parameter type as
written], e.g. ["core-lib/region/Rect", "setRect", "const Rect *"];
every declaration of every matching group becomes a target.
find-chains.py's chains.json gives the triples of a chain.
"""
import json,sys
G=json.load(open(sys.argv[1])); sel=json.load(open(sys.argv[2]))
T=[]; found=set()
for g in G:
    k=(g['key'][0],g['key'][1],g['wt'].replace(' ',''))
    for s in sel:
        if (s[0],s[1],s[2].replace(' ',''))==k:
            found.add(tuple(s))
            for l,c in zip(g['locs'],g['cols']):
                f,ln=l.rsplit(':',1); T.append(dict(file=f,line=int(ln),col=c,names=g['names']))
missing=[s for s in sel if tuple(s) not in found]
json.dump(T,open(sys.argv[3],'w'),indent=1)
print(len(T),'declarations;','missing:',missing)
