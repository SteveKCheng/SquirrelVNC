# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

"""Step 4: mark virtual functions, definitions and null defaults.

Usage: find-declarations.py WORK
Reads WORK/ana.json and the sources in WORK/h; writes WORK/ana2.json.
Works from the source text around each declaration, not the AST:
virtual (virtual or override), pure (= 0), defined (followed by a body
or an initialiser list), and a null default argument (= nullptr, NULL
or 0), which makes a candidate nullable.
"""
import json,re,collections,sys
J=sys.argv[1]; H=J+'/h/'
G=json.load(open(J+'/ana.json'))
src={}
def text(f):
    if f not in src: src[f]=open(H+f,encoding='utf-8',errors='replace').read()
    return src[f]
def offs(f,ln):
    t=text(f); o=0
    for _ in range(ln-1): o=t.index('\n',o)+1
    return o
def info(loc,col):
    f,ln=loc.rsplit(':',1); t=text(f); o=offs(f,int(ln))+col-1
    # back to start of declaration: find '(' opening
    d=0;i=o
    while i>0:
        c=t[i]
        if c==')': d+=1
        elif c=='(':
            if d==0: break
            d-=1
        i-=1
    st=max(t.rfind(';',0,i),t.rfind('}',0,i),t.rfind('{',0,i),t.rfind(':\n',0,i))
    head=t[st+1:i]
    d=0;j=i
    while j<len(t):
        c=t[j]
        if c=='(': d+=1
        elif c==')':
            d-=1
            if d==0: break
        j+=1
    m=re.match(r'[^;{:=]*([;{:=])',t[j+1:],re.S)
    tail=t[j+1:j+1+(m.end() if m else 0)]
    end=m.group(1) if m else '?'
    after=t[j+1:j+40]
    pdecl=t[o:j]; pend=re.search(r'[,)]',t[o:]); seg=t[o:o+(pend.start() if pend else 0)]
    return dict(nulldef=re.search(r'=\s*(nullptr|NULL|0)\b',seg) is not None, virtual='virtual' in head.split() or 'override' in tail or re.search(r'\bvirtual\b',head) is not None,
                pure=re.search(r'=\s*0',after) is not None,
                defn= end in '{:' , deleted=re.search(r'=\s*(delete|default)',after) is not None)
for g in G:
    if g['status']=='skip': continue
    I=[info(l,c) for l,c in zip(g['locs'],g['cols'])]
    g['virtual']=any(x['virtual'] for x in I); g['pure']=any(x['pure'] for x in I); g['defn']=any(x['defn'] for x in I)
    if any(x['nulldef'] for x in I):
        g['flags']=sorted(set(g['flags'])|{'nulldefault'})
        if g['status']=='cand': g['status']='nullable'
json.dump(G,open(J+'/ana2.json','w'),indent=0)
c=collections.Counter((g['status'].split('(')[0],g['virtual'],g['defn']) for g in G if g['status']!='skip' and g['cat']=='obj')
for k,v in sorted(c.items()): print(k,v)
