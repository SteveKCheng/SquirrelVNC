# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

"""Step 2: group declarations and definitions of the same parameter.

Usage: group-params.py WORK
Reads WORK/ptrs.json, writes WORK/grp.json. A group is keyed by file
stem, function name, position and type, so that a header's declaration
and a source's definition land together even when the parameter names
differ, and overloads with different types stay apart.
"""
import json,re,collections,sys,os
S=sys.argv[1]; o=json.load(open(S+'/ptrs.json'))
SCAL=r'(unsignedchar|signedchar|unsignedshort|short|int|unsignedint|long|unsignedlong|longlong|unsignedlonglong|float|double|bool|U?INT(8|16|32|64)|BYTE|WORD|DWORD|size_t|UINT|LONG|ULONG|u?int(8|16|32|64)_t|SOCKET|socklen_t)\*'
for x in o:
    b=re.sub(r'\b(const|volatile|struct|class|enum)\b','',x['wt']).replace(' ','')
    if x['cat']=='obj':
        if re.fullmatch(r'(TCHAR|WCHAR|CHAR|char|wchar_t)\*',b): x['cat']='str'
        elif re.fullmatch(SCAL,b): x['cat']='scalar'
        elif b.endswith('**'): x['cat']='ptrptr'
        elif re.fullmatch(r'(void|VOID)\*',b): x['cat']='void'
g=collections.OrderedDict()
for x in o:
    stem=os.path.splitext(x['f'])[0]
    key=(os.path.normpath(stem),x['fn'].split('::')[-1],x['idx'],x['wt'].replace(' ',''))
    g.setdefault(key,[]).append(x)
print(collections.Counter(v[0]['cat'] for v in g.values()))
json.dump([dict(key=[k[0],k[1],v[-1]['name'] or v[0]['name']],idx=k[2],names=sorted(set(x['name'] for x in v)),cat=v[0]['cat'],wt=v[0]['wt'],locs=[f"{x['f']}:{x['ln']}" for x in v],cols=[x['col'] for x in v],fn=[x['fn'] for x in v]) for k,v in g.items()],open(S+'/grp.json','w'),indent=0)
