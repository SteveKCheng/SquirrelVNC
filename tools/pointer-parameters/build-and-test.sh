#!/bin/bash
# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

# Uploads the files that differ between WORK/h and WORK/rwtree to the
# checkout (tar -m, so that MSBuild sees them as new), builds Debug
# Win32, Debug x64 and ReleaseAsan x64, and runs the tests. For changes
# to the decoders, also build ReleaseFuzz x64 and run run-fuzz.ps1.
# After committing, copy WORK/rwtree's changes into WORK/h (or rerun
# analyse.sh) so that the next batch starts from the new HEAD.
. "$(dirname "$0")/config.sh"
cd "$WORK" || exit 1
files=$(diff -rq h rwtree -x notes | grep '^Files' | awk '{print $2}' | sed 's|^h/||')
tar -cf push.tar -C rwtree $files
claude-ssh put "$HOST" push.tar "${REPO//\\//}/../push.tar" > /dev/null
remote 'git status --short; tar -xmf ..\push.tar; git diff --stat | Select-Object -Last 1'
msbuild='C:\Program Files\Microsoft Visual Studio\18\Community\MSBuild\Current\Bin\MSBuild.exe'
for c in "Debug Win32" "Debug x64" "ReleaseAsan x64"; do
  set -- $c
  echo "== $1 $2"
  remote "& '$msbuild' SquirrelVNC.slnx /m /t:Build /p:Configuration=$1 /p:Platform=$2 /v:quiet /nologo '/flp:logfile=..\\build.log;verbosity=minimal' | Out-Null; \"exit=\$LASTEXITCODE\"; Select-String -Path ..\\build.log -Pattern '(error|warning) [A-Z]+[0-9]+' | ForEach-Object { \$_.Line } | Sort-Object -Unique | Select-Object -First 30"
done
remote 'pwsh -File tests\run-tests.ps1 -Configuration ReleaseAsan -Platform x64' | tail -1
