# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

"""Step 5: attach the call-site arguments that need reading by hand.

Usage: attach-call-args.py WORK
Reads WORK/ana2.json and WORK/arg.out (find-call-args.query); writes
WORK/ana3.json, the final per-parameter data, with each group's
non-obvious call-site arguments (file:line and source line).
"""
import re,json,collections,sys
J=sys.argv[1]; PRE='C:\\Users\\sandbox\\dev\\tightvnc-2.8.90-gpl\\'
G=json.load(open(J+'/ana2.json'))
loc2g={}
for i,g in enumerate(G):
    for loc in g['locs']:
        f,ln=loc.rsplit(':',1)
        for nm in g['names']: loc2g[(f,int(ln),nm)]=i
rxp=re.compile(r"^ParmVarDecl 0x\w+ <([^<>,]+?):(\d+):\d+[^>]*> (?:col:\d+|line:\d+:\d+)(?: used| referenced)?(?: (\w+))? '")
args=collections.defaultdict(set)
blk=[]
def flush():
    a=None;d=None;i=0
    while i<len(blk):
        l=blk[i]
        m=re.match(r'^(.*)\((\d+),(\d+)\): note: "arg" binds here',l)
        if m and i+1<len(blk): a=(m.group(1).replace(PRE,'').replace('\\','/')+':'+m.group(2), blk[i+1].split('|',1)[1].strip())
        m=rxp.match(l)
        if m and m.group(1).startswith(PRE): d=(m.group(1)[len(PRE):].replace('\\','/'),int(m.group(2)),m.group(3) or '')
        i+=1
    if a and d and d in loc2g and 'vcpkg' not in a[0]: args[loc2g[d]].add(a)
for l in open(J+'/arg.out',encoding='utf-8',errors='replace'):
    l=l.rstrip('\n')
    if l.startswith('Match #'): flush(); blk=[]
    else: blk.append(l)
flush()
for i,g in enumerate(G): g['args']=sorted(args.get(i,()))
json.dump(G,open(J+'/ana3.json','w'),indent=0)
print(sum(1 for g in G if g['status']=='cand' and g['args']), sum(1 for g in G if g['status']=='cand'))
