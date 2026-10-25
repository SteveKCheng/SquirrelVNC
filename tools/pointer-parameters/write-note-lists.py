# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

"""Write the candidate lists of notes/pointer-parameters.md.

Usage: write-note-lists.py WORK
Reads WORK/ana3.json; writes WORK/report-cands.md (virtual families and
other candidates by file) and prints the nullable parameters. Used for
the survey's lists; not needed for converting.
"""
import json,collections,sys,re
J=sys.argv[1]; G=json.load(open(J+'/ana3.json'))
O=[g for g in G if g['cat'] in('obj',) and g['status']!='skip']
def cls(g): return g['key'][0].split('/')[-1]
def where(g):
    hs=[l for l in g['locs'] if l.endswith('.h')]; return (hs or g['locs'])[0]
def ty(g): return g['wt'].replace('class ','').replace('struct ','')
fam=collections.defaultdict(list)
for g in O:
    if g['virtual']: fam[(g['key'][1],g['idx'],g['wt'].replace(' ',''))].append(g)
famcand=set(); out=[]
out.append('### Virtual-function families\n')
out.append('A base declaration and its overrides must change together; all members of these families are borrows.\n')
rows=[]
for (m,i,_),v in fam.items():
    st=set(x['status'].split('(')[0] for x in v)
    if st!={'cand'}:
        for x in v: famcand.add(id(x))
        continue
    for x in v: famcand.add(id(x))
    classes=sorted(set(cls(x) for x in v))
    rows.append((classes[0] if len(classes)==1 else '/'.join(classes[:1]), m, i, ty(v[0]), ', '.join(classes)))
bym=collections.defaultdict(list)
for r in rows: bym[(r[4],r[1])].append((r[2],r[3]))
for (classes,m),ps in sorted(bym.items(), key=lambda kv:(kv[0][0],kv[0][1])):
    ps=sorted(ps); out.append(f"- `{m}` ({', '.join(f'#{i+1} `{t}`' for i,t in ps)}): {classes}")
out.append('\n### Other functions, by file\n')
byf=collections.defaultdict(list)
for g in O:
    if g['status']!='cand' or id(g) in famcand: continue
    if g['key'][1] in ('?',): continue
    byf[where(g).rsplit(':',1)[0]].append(g)
n_other=0
for f in sorted(byf):
    gs=byf[f]; fm=collections.defaultdict(list)
    for g in gs: fm[g['key'][1]].append(g)
    items=[]
    for m,v in fm.items():
        n_other+=len(v)
        items.append(f"`{m}` ({', '.join(f'`{x['key'][2] or '#'+str(x['idx']+1)}`' for x in sorted(v,key=lambda x:x['idx']))})")
    out.append(f"- `{f}`: " + ', '.join(items))
open(J+'/report-cands.md','w').write('\n'.join(out)+'\n')
print(len(rows),'family params', len(bym),'families;', n_other,'other params in',len(byf),'files')
# nullable / stash lists
nl=[g for g in O if g['status'].startswith('nullable')]
print('\n'.join(f"- `{cls(g)}::{g['key'][1]}` `{g['key'][2]}` ({', '.join(g['flags'])}) {where(g)}" for g in nl))
