# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

<#
.SYNOPSIS
Runs the replay tests in tests/cases with rfb-replay, and the fuzz inputs
in tests/fuzz with rfb-fuzz.

.DESCRIPTION
Each case is one command, rfb-replay --check, which compares the client's
bytes, the event log and the final frame buffer itself and exits with 0
for a match. The fuzz corpus and regression inputs are one more command,
rfb-fuzz with every file, which runs each once and exits with 0 if none
of them crashed. This script only runs those commands and counts
failures, so that CTest can take over when CMake builds the core
(notes/testing.md).

A configuration that lacks one of the programs (ReleaseFuzz has no
rfb-replay) skips its tests.

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
$bin = Join-Path $root "bin\$Platform\$Configuration"
$replay = Join-Path $bin 'rfb-replay.exe'
$fuzz = Join-Path $bin 'rfb-fuzz.exe'
if (-not (Test-Path $replay) -and -not (Test-Path $fuzz)) {
  Write-Error "Neither rfb-replay.exe nor rfb-fuzz.exe is in $bin (build the solution for $Configuration|$Platform first)"
}

function Get-Reason([int]$code) {
  switch ($code) {
    1 { 'mismatch' }
    3 { 'timeout' }
    4 { 'AddressSanitizer report' }
    default { "exit code $code" }
  }
}

$total = 0
$failed = @()

if (Test-Path $replay) {
  $cases = Get-ChildItem (Join-Path $PSScriptRoot 'cases') -Filter "$Filter.rfb" |
    Sort-Object Name
  foreach ($case in $cases) {
    $name = $case.BaseName
    $prefix = Join-Path $case.DirectoryName $name
    $total++
    # The core's log goes to stderr; show it only for failures.
    $output = & $replay --check $prefix --log 1 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    if ($code -eq 0) {
      Write-Host "PASS $name"
    } else {
      $failed += $name
      Write-Host "FAIL $name ($(Get-Reason $code))"
      $output | ForEach-Object { Write-Host "     $_" }
    }
  }
} else {
  Write-Host "SKIP replay cases: rfb-replay is not built in $Configuration"
}

if (Test-Path $fuzz) {
  $inputs = @('corpus', 'regressions') | ForEach-Object {
    $dir = Join-Path $PSScriptRoot "fuzz\$_"
    if (Test-Path $dir) { Get-ChildItem $dir -File -Filter "$Filter.corpus" | Sort-Object Name }
  }
  if ($inputs.Count -gt 0) {
    $total++
    $output = & $fuzz $inputs.FullName 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    if ($code -eq 0) {
      Write-Host "PASS fuzz inputs ($($inputs.Count) files)"
    } else {
      $failed += 'fuzz inputs'
      Write-Host "FAIL fuzz inputs ($(Get-Reason $code))"
      $output | Select-Object -Last 40 | ForEach-Object { Write-Host "     $_" }
    }
  }
}

Write-Host ''
Write-Host ("{0} of {1} tests passed ({2}|{3})" -f
  ($total - $failed.Count), $total, $Configuration, $Platform)
if ($failed.Count -gt 0) {
  exit 1
}
