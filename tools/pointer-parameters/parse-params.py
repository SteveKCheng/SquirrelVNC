# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

"""Step 1: parse find-params.query's output into ptrs.json.

Usage: parse-params.py WORK
Reads WORK/ptr.out (ParmVarDecl dumps from every source) and the
sources in WORK/h (a git archive of the commit analysed). Writes
WORK/ptrs.json: one entry per parameter declaration, with its file,
line, column, name, type as written, enclosing function, position in
the parameter list, and a rough category (obj, str, typedef, fnptr,
void, scalar, ptrptr). Only obj (pointers to class types) are
candidates; str (char/TCHAR strings) and typedef (HWND, LPARAM...) are
out of scope.
"""
import re, sys, collections, json
S=sys.argv[1]; H=S+'/h/'
rx=re.compile(r"^ParmVarDecl 0x\w+ <(?:C:\\Users\\sandbox\\dev\\tightvnc-2\.8\.90-gpl\\)([^:<>]+):(\d+):(\d+)[^>]*> (?:col:\d+|line:\d+:\d+)(?: used| referenced)?(?: (\w+))? '([^']*)'(?::'([^']*)')?")
seen={}
for l in open(S+'/ptr.out',encoding='utf-8',errors='replace'):
    if 'vcpkg_installed' in l or 'tightvnc-2.8.90-gpl' not in l: continue
    m=rx.match(l)
    if not m: print('NOMATCH',l.strip(),file=sys.stderr); continue
    f,ln,col,name,wt,ct=m.groups(); f=f.replace('\\','/'); ct=ct or wt
    seen[(f,int(ln),int(col))]=(name or '',wt,ct)
src={}
def text(f):
    if f not in src: src[f]=open(H+f,encoding='utf-8',errors='replace').read().split('\n')
    return src[f]
def func(f,ln,col):
    L=text(f); s='\n'.join(L[:ln-1])+'\n'+L[ln-1][:col-1]
    d=0;i=len(s)-1
    while i>=0:
        c=s[i]
        if c==')': d+=1
        elif c=='(':
            if d==0: break
            d-=1
        i-=1
    m=re.search(r'([~\w:<>]+)\s*$',s[:i])
    seg=s[i+1:]; d=0; n=0
    for c in seg:
        if c in '(<[{': d+=1
        elif c in ')>]}': d-=1
        elif c==',' and d==0: n+=1
    return (m.group(1) if m else '?'), n
out=[]
for (f,ln,col),(name,wt,ct) in sorted(seen.items()):
    pointee=ct.rstrip()
    cat='obj'
    base=re.sub(r'\b(const|volatile|struct|class|enum)\b','',ct).replace(' ','')
    if '(' in ct: cat='fnptr'
    elif '*' not in wt and '[' not in wt: cat='typedef'
    elif re.fullmatch(r'(char|wchar_t|char8_t|char16_t)\*',base): cat='str'
    elif base.endswith('**') : cat='ptrptr'
    elif re.fullmatch(r'void\*',base): cat='void'
    elif re.fullmatch(r'(unsignedchar|signedchar|unsignedshort|short|int|unsignedint|long|unsignedlong|longlong|unsignedlonglong|float|double|bool)\*',base): cat='scalar'
    out.append(dict(f=f,ln=ln,col=col,name=name,wt=wt,ct=ct,cat=cat,fn=func(f,ln,col)[0],idx=func(f,ln,col)[1]))
json.dump(out,open(S+'/ptrs.json','w'),indent=0)
print(collections.Counter(o['cat'] for o in out))
