# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

"""Step 6: closed chains of candidates, to choose a batch.

Usage: find-chains.py WORK
Reads WORK/ana3.json; writes WORK/chains.json and prints the biggest.
Parameters that pass the pointer on to one another, and the members of
a virtual family (same name, position and type), are joined into one
chain; a chain can be converted in one commit without temporary &/* at
its edges. Restricted to const pointees; edit the filter for others.
"""
import json,collections,sys
J=sys.argv[1]; G=json.load(open(J+'/ana3.json'))
loc2g={}
for i,g in enumerate(G):
  for l in g['locs']:
    f,ln=l.rsplit(':',1)
    for nm in g['names']: loc2g[(f,int(ln),nm)]=i
C={i for i,g in enumerate(G) if g['cat']=='obj' and g['wt'].replace(' ','').startswith('const') and g['status']!='skip'}
par={i:i for i in C}
def f(x):
  while par[x]!=x: par[x]=par[par[x]]; x=par[x]
  return x
def u(a,b): par[f(a)]=f(b)
for i in C:
  for d in G[i]['edges']:
    j=loc2g.get((d[1],int(d[2]),d[3])) if d[0]=='P' else None
    if j in C: u(i,j)
fam=collections.defaultdict(list)
for i in C:
  if G[i]['virtual']: fam[(G[i]['key'][1],G[i]['idx'],G[i]['wt'].replace(' ',''))].append(i)
for v in fam.values():
  for x in v[1:]: u(v[0],x)
comp=collections.defaultdict(list)
for i in C: comp[f(i)].append(i)
out=[]
for r,v in comp.items():
  out.append(dict(size=len(v),types=sorted(set(G[i]['wt'] for i in v)),
    members=[dict(key=G[i]['key'],idx=G[i]['idx'],wt=G[i]['wt'],status=G[i]['status'],flags=G[i]['flags'],virtual=G[i]['virtual'],
                  args=G[i]['args'],localctor=G[i].get('localctor',[]),std=[e[1] for e in G[i]['edges'] if 'MSVC' in e[1]]) for i in v]))
out.sort(key=lambda c:-c['size'])
json.dump(out,open(J+'/chains.json','w'),indent=0)
for c in out[:4]: print(c['size'],c['types'],collections.Counter(m['status'].split('(')[0] for m in c['members']))
