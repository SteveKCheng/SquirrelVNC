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
# Started with its output in a file: piped through PowerShell
# ("2>&1 | Select-Object"), rfb-fuzz did not stop at -max_total_time
# (notes/testing.md). As a safeguard, it is also stopped from here, a
# little after the time is up.
$log = "$w\fuzz.log"
$p = Start-Process -FilePath bin\x64\ReleaseFuzz\rfb-fuzz.exe -NoNewWindow -PassThru `
  -RedirectStandardError $log `
  -ArgumentList "-max_total_time=$Seconds", '-timeout=10', '-rss_limit_mb=0',
                "-artifact_prefix=$w\", "$w\corpus"
if (-not $p.WaitForExit(($Seconds + 30) * 1000)) {
  Stop-Process -Id $p.Id -Force
  $p.WaitForExit()
  "stopped after $Seconds seconds"
} else {
  "exit=$($p.ExitCode)"
}
Get-Content $log | Select-Object -Last 3
Get-ChildItem $w -File -Exclude fuzz.log | Select-Object -ExpandProperty Name
