#!/bin/bash
# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

# Usage: rewrite.sh SELECTION.json
# Converts the selected parameters (steps 7 to 9) in a fresh copy of the
# analysed tree, WORK/rwtree, and prints what it did and any use it left
# for editing by hand. Then: read the diff (diff -ru WORK/h WORK/rwtree),
# make the hand edits in WORK/rwtree, and run build-and-test.sh.
#
# Hand edits seen so far: std::make_unique calls (reported as "other
# use"; no edit is needed once the forwarding parameter is a reference,
# but a make_unique that passes &x must lose the &), AutoLock al(p)
# (becomes &p), doc comments that said "must not be null", and an
# existing reference overload (Region::add) that the conversion
# duplicates. The compiler catches what is missed.
. "$(dirname "$0")/config.sh"
sel=$1
python3 "$TOOLS/make-targets.py" "$WORK/grp.json" "$sel" "$WORK/targets.json" || exit 1
python3 "$TOOLS/make-rewrite-query.py" "$WORK/targets.json" > "$WORK/rw.query"
claude-ssh put "$HOST" "$WORK/rw.query" "${REPO//\\//}/../rw.query" > /dev/null
remote "pwsh $TOOLS_REMOTE\\run-query.ps1 ..\\rw.query ..\\rw.out"
claude-ssh get "$HOST" "${REPO//\\//}/../rw.out" "$WORK/rw.out" > /dev/null
rm -rf "$WORK/rwtree" && mkdir "$WORK/rwtree"
tar -cf - -C "$WORK/h" win32-lib core-lib tvnviewer harnesses tools | tar -xf - -C "$WORK/rwtree"
python3 "$TOOLS/apply-rewrite.py" "$WORK/targets.json" "$WORK/rw.out" "$WORK/rwtree" > "$WORK/apply.log"
grep -E 'edits in|PROBLEM' "$WORK/apply.log"
