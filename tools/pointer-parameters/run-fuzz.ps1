# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

<#
.SYNOPSIS
Runs rfb-fuzz for a while on a copy of the corpus and regression inputs,
as a check after converting code the decoders use.

.DESCRIPTION
Build ReleaseFuzz x64 first. Run from the top of the tree. The working
directory is recreated each time; libFuzzer writes any crashing input
to it, which the last line lists. See notes/testing.md.
#>
param(
  [string] $WorkDir = '..\fuzz-ptr',
  [int] $Seconds = 120
)
if (Test-Path $WorkDir) { Remove-Item -Recurse -Force $WorkDir }
New-Item -ItemType Directory "$WorkDir\corpus" | Out-Null
Copy-Item tests\fuzz\corpus\* "$WorkDir\corpus"
Copy-Item tests\fuzz\regressions\* "$WorkDir\corpus"
$w = (Resolve-Path $WorkDir).Path
& bin\x64\ReleaseFuzz\rfb-fuzz.exe -max_total_time=$Seconds -timeout=10 -rss_limit_mb=0 "-artifact_prefix=$w\" "$w\corpus" 2>&1 | Select-Object -Last 3
"exit=$LASTEXITCODE"
Get-ChildItem $w -File | Select-Object -ExpandProperty Name
