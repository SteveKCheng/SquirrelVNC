# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

"""Step 8: write the clang-query file for apply-rewrite.py.

Usage: make-rewrite-query.py TARGETS.json > rw.query
Collects every use of the target parameters (by name; apply-rewrite.py
filters them by declaration location): all references, those under
->, those under *, and the call-site arguments bound to them.
"""
import json,sys
T=json.load(open(sys.argv[1]))
names=sorted({n for t in T for n in t['names'] if n})
P=('parmVarDecl(hasAnyName(%s), hasType(qualType(hasCanonicalType(pointerType()))), unless(isExpansionInSystemHeader()))'
   % ', '.join('"%s"' % n for n in names))
R='declRefExpr(to(%s.bind("p"))).bind("ref")' % P
print('set output dump\nset bind-root false\nset traversal AsIs')
print('match %s' % R)
print('match memberExpr(isArrow(), hasObjectExpression(ignoringParenImpCasts(%s))).bind("arrow")' % R)
print('match unaryOperator(hasOperatorName("*"), hasUnaryOperand(ignoringParenImpCasts(%s))).bind("deref")' % R)
for k in ('callExpr','cxxConstructExpr'):
    print('match %s(forEachArgumentWithParam(expr().bind("arg"), %s.bind("p")))' % (k,P))
