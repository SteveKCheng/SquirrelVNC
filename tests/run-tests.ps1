# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

<#
.SYNOPSIS
Runs the replay tests in tests/cases with rfb-replay.

.DESCRIPTION
Each case is one command, rfb-replay --check, which compares the client's
bytes, the event log and the final frame buffer itself and exits with 0
for a match. This script only runs those commands and counts failures,
so that CTest can take over when CMake builds the core
(notes/testing.md).

Build the solution first, in the same configuration and platform.

.EXAMPLE
pwsh tests/run-tests.ps1 -Configuration Debug -Platform Win32
#>
param(
  [string]$Configuration = 'Release',
  [string]$Platform = 'x64',
  # Run only the cases whose names match this wildcard.
  [string]$Filter = '*'
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $PSScriptRoot
$replay = Join-Path $root "bin\$Platform\$Configuration\rfb-replay.exe"
if (-not (Test-Path $replay)) {
  Write-Error "Not found: $replay (build the solution for $Configuration|$Platform first)"
}

$cases = Get-ChildItem (Join-Path $PSScriptRoot 'cases') -Filter "$Filter.rfb" |
  Sort-Object Name
$failed = @()
foreach ($case in $cases) {
  $name = $case.BaseName
  $prefix = Join-Path $case.DirectoryName $name
  # The core's log goes to stderr; show it only for failures.
  $output = & $replay --check $prefix --log 1 2>&1 | ForEach-Object { "$_" }
  $code = $LASTEXITCODE
  if ($code -eq 0) {
    Write-Host "PASS $name"
  } else {
    $failed += $name
    $why = switch ($code) {
      1 { 'mismatch' }
      3 { 'timeout' }
      4 { 'AddressSanitizer report' }
      default { "exit code $code" }
    }
    Write-Host "FAIL $name ($why)"
    $output | ForEach-Object { Write-Host "     $_" }
  }
}

Write-Host ''
Write-Host ("{0} of {1} cases passed ({2}|{3})" -f
  ($cases.Count - $failed.Count), $cases.Count, $Configuration, $Platform)
if ($failed.Count -gt 0) {
  exit 1
}
