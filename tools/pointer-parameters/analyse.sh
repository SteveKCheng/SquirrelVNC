#!/bin/bash
# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

# Runs the whole analysis on the checkout's HEAD: the four queries on the
# Windows machine, then steps 1 to 6 locally. Takes about 15 minutes,
# nearly all of it in clang-query. Rerun it before each batch: the
# chains and the line numbers change as code is converted or removed.
#
# Afterwards: WORK/ana3.json has every pointer parameter with its status,
# and WORK/chains.json the chains of candidates.
. "$(dirname "$0")/config.sh"
mkdir -p "$WORK"
remote "git archive -o ..\\head.tar HEAD; pwsh tools/clang-tidy/make-compile-commands.ps1 -Configuration Debug -Platform x64 | Out-Null"
for q in find-params:ptr find-uses:use find-forwarding:fwd find-call-args:arg; do
  remote "pwsh $TOOLS_REMOTE\\run-query.ps1 $TOOLS_REMOTE\\${q%%:*}.query ..\\${q#*:}.out"
done
for f in head.tar ptr.out use.out fwd.out arg.out; do
  claude-ssh get "$HOST" "${REPO//\\//}/../$f" "$WORK/$f" > /dev/null
done
rm -rf "$WORK/h" && mkdir "$WORK/h" && tar -xf "$WORK/head.tar" -C "$WORK/h"
for s in parse-params group-params classify-uses find-declarations attach-call-args find-chains; do
  python3 "$TOOLS/$s.py" "$WORK" > /dev/null || exit 1
done
echo "analysis written to $WORK"
