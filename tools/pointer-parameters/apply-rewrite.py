# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

"""Step 9: convert `T *p` parameters to `T &p`, from clang-query output.

Usage: apply-rewrite.py targets.json rw.out SRCROOT
The targets come from make-targets.py, and rw.out from running
make-rewrite-query.py's query with run-query.ps1.
Edits files under srcroot in place, and prints what it did and any use
of a parameter it could not convert (to be edited by hand).
"""
import json,re,sys,collections
T=json.load(open(sys.argv[1])); OUT=sys.argv[2]; ROOT=sys.argv[3].rstrip('/')+'/'
PRE='C:\\Users\\sandbox\\dev\\tightvnc-2.8.90-gpl\\'
targets={(t['file'],t['line'],t['col']) for t in T}
rx=re.compile(r"^(\w+) 0x\w+ <(.+?):(\d+):(\d+)(?:, (?:col:(\d+)|line:(\d+):(\d+)))?>(.*)$")
def node(l):
    m=rx.match(l)
    if not m: return None
    kind,path,ln,col,ec,el,ec2,rest=m.groups()
    if not path.startswith(PRE) or 'vcpkg_installed' in path: return None
    f=path[len(PRE):].replace('\\','/'); ln=int(ln); col=int(col)
    end=(ln,int(ec)) if ec else ((int(el),int(ec2)) if el else (ln,col))
    return dict(kind=kind,file=f,begin=(ln,col),end=end,rest=rest)
matches=[]; cur=None; name=None
for l in open(OUT,encoding='utf-8',errors='replace'):
    l=l.rstrip('\n')
    if l.startswith('Match #'): cur={}; matches.append(cur); continue
    m=re.match(r'Binding for "(\w+)":',l)
    if m: name=m.group(1); continue
    if cur is not None and name:
        n=node(l)
        cur[name]=n; name=None
def tkey(n): return (n['file'],)+n['begin']
refs={}; arrow=set(); deref={}; args={}; pname={}
for m in matches:
    p=m.get('p')
    if not p or tkey(p) not in targets: continue
    mm=re.search(r'> col:(\d+)(?: used| referenced)? (\w+) ',p['rest'] if p['rest'].startswith('>') else '>'+p['rest'])
    r=m.get('ref')
    if r: refs[tkey(r)]=(r,p)
    if 'arrow' in m and r: arrow.add(tkey(r))
    if 'deref' in m and m['deref'] and r: deref[tkey(r)]=m['deref']
    if 'arg' in m and m['arg']: args[tkey(m['arg'])]=m['arg']
# source access, in bytes (clang columns count bytes)
src={}
def lines(f):
    if f not in src: src[f]=open(ROOT+f,'rb').read().split(b'\n')
    return src[f]
def off(f,lc): return lc  # (line, col) kept as is; edits are per line
edits=collections.defaultdict(dict)   # file -> {(line,col): (oldlen, new)}
problems=[]
def edit(f,line,col,old,new,why):
    L=lines(f)[line-1]
    if L[col-1:col-1+len(old)]!=old:
        problems.append(f"{f}:{line}:{col}: expected {old!r} for {why}, found {L[col-1:col-1+len(old)+8]!r}"); return
    edits[f][(line,col)]=(len(old),new,why)
# declarations: replace the last '*' between the type and the name
for (f,line,col) in sorted(targets):
    L=lines(f)[line-1]
    t=next(t for t in T if (t['file'],t['line'],t['col'])==(f,line,col))
    seg=L[col-1:]
    # The last '*' before the parameter's name (or before ',' or ')' when unnamed).
    names=[n for n in t['names'] if n]
    m=None
    for n in names:
        m=re.match(rb'(.*?)\*(?:\s*/\*)?\s*'+re.escape(n.encode())+rb'\b',seg)
        if m: break
    if not m and not names:
        m=re.match(rb'([^,()]*?)\*\s*[,)]',seg)
    if not m:
        problems.append(f"{f}:{line}:{col}: cannot find the '*' of the declaration"); continue
    edit(f,line,col+len(m.group(1)),b'*',b'&','declaration')
for k,(r,p) in refs.items():
    f,line,col=k; L=lines(f)[line-1]
    nm=re.match(rb'\w+',L[col-1:]).group(0)
    if k in arrow:
        m=re.match(rb'(\s*)->',L[col-1+len(nm):])
        if not m: problems.append(f"{f}:{line}:{col}: no '->' after {nm!r}"); continue
        edit(f,line,col+len(nm)+len(m.group(1)),b'->',b'.','p-> to p.')
    elif k in deref:
        d=deref[k]; dl,dc=d['begin']
        if d['file']!=f or dl!=line: problems.append(f"{f}:{line}:{col}: '*' on another line"); continue
        between=L[dc-1:col-1]
        if not re.fullmatch(rb'\*[\s(]*',between): problems.append(f"{f}:{line}:{col}: unexpected text {between!r} in deref"); continue
        edit(f,line,dc,b'*',b'','*p to p')
    elif k in args:
        pass   # passed on to another converted parameter: unchanged
    else:
        problems.append(f"{f}:{line}:{col}: other use of {nm.decode()}: {L.strip().decode(errors='replace')}")
# call sites
def tok_end(f,line,col):
    L=lines(f)[line-1]
    m=re.match(rb'\w+|->|::|.',L[col-1:]); return col+len(m.group(0))
for k,a in args.items():
    if k in refs: continue   # a converted parameter passed on
    f=a['file']; (bl,bc)=a['begin']; (el,ec)=a['end']
    if bl!=el: problems.append(f"{f}:{bl}:{bc}: argument spans lines; edit by hand"); continue
    L=lines(f)[bl-1]; e=tok_end(f,el,ec); text=L[bc-1:e-1]
    if a['kind']=='UnaryOperator' and text.startswith(b'&'):
        edit(f,bl,bc,b'&',b'','&x to x at call')
    elif re.fullmatch(rb'\w+((\.|->|::)\w+(\(\))?)*',text):
        edit(f,bl,bc,b'',b'*','x to *x at call')
    else:
        edit(f,bl,bc,b'',b'*(','e to *(e) at call'); edit(f,bl,e,b'',b')','e to *(e) at call')
# apply, right to left on each line
n=0
for f,E in edits.items():
    L=lines(f)
    for (line,col),(ol,new,why) in sorted(E.items(),reverse=True):
        s=L[line-1]; L[line-1]=s[:col-1]+new+s[col-1+ol:]; n+=1
        print(f"{f}:{line}:{col}: {why}")
    open(ROOT+f,'wb').write(b'\n'.join(L))
print(f"{n} edits in {len(edits)} files; {len(problems)} problems")
for p in problems: print('PROBLEM',p)
