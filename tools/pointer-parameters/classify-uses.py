# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

"""Step 3: classify each parameter group from how its body uses it.

Usage: classify-uses.py WORK
Reads WORK/grp.json, WORK/use.out (find-uses.query) and WORK/fwd.out
(find-forwarding.query). Writes WORK/ana.json with, per group, the use
flags, the parameters it is passed on to, and a status:
  cand         borrowed and never null-checked: a candidate;
  stash/array  kept after the call, or used as an array;
  nullable     null-checked, or given null by some caller;
  review       passed to something the analysis cannot follow.
A group passed on to a kept or nullable parameter takes that status
too, repeated until nothing changes (shown as "(fwd)").
"""
import re,json,collections,sys
J=sys.argv[1]
G=json.load(open(J+'/grp.json'))
rx=re.compile(r"^ParmVarDecl 0x\w+ <([^:<>,]+(?::[^:<>,]+)?):(\d+):\d+[^>]*> (?:col:\d+|line:\d+:\d+)(?: used| referenced)?(?: (\w+))? '([^']*)'")
PRE='C:\\Users\\sandbox\\dev\\tightvnc-2.8.90-gpl\\'
def key(l):
    m=rx.match(l)
    if not m: return None
    p,ln,name,t=m.groups()
    if p.startswith(PRE) and 'vcpkg_installed' not in p: return ('P',p[len(PRE):].replace('\\','/'),int(ln),name or '',t)
    return ('S',p,int(ln),name or '',t)
flags=collections.defaultdict(set)
cur=None
for l in open(J+'/use.out',encoding='utf-8',errors='replace'):
    m=re.match(r'Binding for "(\w+)"',l)
    if m: cur=m.group(1); continue
    k=key(l)
    if k and k[0]=='P' and cur: flags[(k[1],k[2],k[3])].add(cur); cur=None
edges=collections.defaultdict(set)
b={}
local=collections.defaultdict(set)
def flush():
    if 'lsrc' in b and 'ldst' in b and b['lsrc'][0]=='P': local[(b['lsrc'][1],b['lsrc'][2],b['lsrc'][3])].add(b['ldst'])
    if 'src' in b and 'dst' in b:
        s=b['src']; d=b['dst']
        if s[0]=='P': edges[(s[1],s[2],s[3])].add(d)
for l in open(J+'/fwd.out',encoding='utf-8',errors='replace'):
    if l.startswith('Match #'): flush(); b={}; cur=None; continue
    m=re.match(r'Binding for "(\w+)"',l)
    if m: cur=m.group(1); continue
    k=key(l)
    if k and cur: b[cur]=k; cur=None
flush()
# map loc->group index
loc2g={}
for i,g in enumerate(G):
    g['flags']=set(); g['edges']=set()
    for loc in g['locs']:
        f,ln=loc.rsplit(':',1)
        for nm in g['names']: loc2g[(f,int(ln),nm)]=i
for k,v in flags.items():
    if k in loc2g: G[loc2g[k]]['flags']|=v
for k,v in edges.items():
    if k in loc2g: G[loc2g[k]]['edges']|=(v-local.get(k,set()))
for k,v in local.items():
    if k in loc2g: G[loc2g[k]].setdefault('localctor',set()).update(v)
BAD={'ctorinit','assign','alias','ret','lambda','delete','index','arith'}
NUL={'nullcmp','nullbool','passnull'}
def edge_target(d):
    if d[0]=='S':
        if '\\MSVC\\' in d[1] or 'vcpkg' in d[1]: return 'std'
        return 'api'
    return loc2g.get((d[1],d[2],d[3]),'unknown:'+d[4])
status={}
for i,g in enumerate(G):
    fl=g['flags']
    if g['cat'] not in ('obj','scalar','ptrptr','void'): status[i]='skip'; continue
    if fl & BAD: status[i]='stash/array'
    elif fl & NUL: status[i]='nullable'
    else: status[i]='cand'
    g['tg']=[edge_target(d) for d in g['edges']]
# Checked by hand: the std edge is memset, or make_unique of a class whose constructor only reads the pointee.
STD_OK={('win32-lib/win-system/WindowsApplication','registerWindowClass'),('tvnviewer/TvnViewer','registerWindowClass'),
        ('win32-lib/gui/DibFrameBuffer','updateDibSection'),('tvnviewer/TvnViewer','runInstance')}
for i,g in enumerate(G):
    if tuple(g['key'][:2]) in STD_OK: g['tg']=[t for t in g.get('tg',[]) if t!='std']
changed=True
while changed:
    changed=False
    for i,g in enumerate(G):
        if status[i]!='cand': continue
        for t in g['tg']:
            st = 'stash/array(fwd std)' if t=='std' else None
            if isinstance(t,int) and status[t] not in ('cand','skip'): st=status[t]+'(fwd)'
            if isinstance(t,str) and t.startswith('unknown'): st='review(fwd '+t+')'
            if st: status[i]=st; changed=True; break
for i,g in enumerate(G): g['status']=status[i]; g['flags']=sorted(g['flags']); g['edges']=[list(map(str,d)) for d in g['edges']]; g['localctor']=[d[1].split(chr(92))[-1]+':'+str(d[2]) for d in g.get('localctor',())]
json.dump(G,open(J+'/ana.json','w'),indent=0,default=str)
print(collections.Counter((g['cat'],g['status'].split('(')[0]) for g in G if g['status']!='skip'))
