# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

<#
.SYNOPSIS
Runs the replay tests in tests/cases with rfb-replay, the unit tests in
ini-test, the fuzz inputs in tests/fuzz with rfb-fuzz, and those in
tests/fuzz-ini with ini-fuzz.

.DESCRIPTION
Each case is one command, rfb-replay --check, which compares the client's
bytes, the event log and the final frame buffer itself and exits with 0
for a match. The fuzz corpus and regression inputs are one more command,
rfb-fuzz with every file, which runs each once and exits with 0 if none
of them crashed. This script only runs those commands and counts
failures, so that CTest can take over when CMake builds the core
(notes/testing.md).

ini-test is one more command, which runs its own checks and exits with 0
if all of them passed.

A configuration that lacks one of the programs (ReleaseFuzz has no
rfb-replay or ini-test) skips its tests.

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
$iniTest = Join-Path $bin 'ini-test.exe'
$iniFuzz = Join-Path $bin 'ini-fuzz.exe'
if (-not (Test-Path $replay) -and -not (Test-Path $fuzz) -and -not (Test-Path $iniTest)) {
  Write-Error "None of rfb-replay.exe, ini-test.exe and rfb-fuzz.exe is in $bin (build the solution for $Configuration|$Platform first)"
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

if ((Test-Path $iniTest) -and 'ini-test' -like $Filter) {
  $total++
  $output = & $iniTest 2>&1 | ForEach-Object { "$_" }
  $code = $LASTEXITCODE
  if ($code -eq 0) {
    Write-Host "PASS ini-test"
  } else {
    $failed += 'ini-test'
    Write-Host "FAIL ini-test ($(Get-Reason $code))"
    $output | Where-Object { $_ -notmatch '^PASS ' } | ForEach-Object { Write-Host "     $_" }
  }
} elseif (-not (Test-Path $iniTest)) {
  Write-Host "SKIP ini-test: it is not built in $Configuration"
}

# Runs a fuzz program once on the corpus and regression inputs in tests\$dirName.
function Test-FuzzInputs([string]$program, [string]$dirName, [string]$label) {
  if (-not (Test-Path $program)) {
    return
  }
  $inputs = @('corpus', 'regressions') | ForEach-Object {
    $dir = Join-Path $PSScriptRoot "$dirName\$_"
    if (Test-Path $dir) { Get-ChildItem $dir -File -Filter "$Filter.corpus" | Sort-Object Name }
  }
  if ($inputs.Count -gt 0) {
    $script:total++
    $output = & $program $inputs.FullName 2>&1 | ForEach-Object { "$_" }
    $code = $LASTEXITCODE
    if ($code -eq 0) {
      Write-Host "PASS $label ($($inputs.Count) files)"
    } else {
      $script:failed += $label
      Write-Host "FAIL $label ($(Get-Reason $code))"
      $output | Select-Object -Last 40 | ForEach-Object { Write-Host "     $_" }
    }
  }
}

Test-FuzzInputs $fuzz 'fuzz' 'fuzz inputs'
Test-FuzzInputs $iniFuzz 'fuzz-ini' 'ini fuzz inputs'

Write-Host ''
Write-Host ("{0} of {1} tests passed ({2}|{3})" -f
  ($total - $failed.Count), $total, $Configuration, $Platform)
if ($failed.Count -gt 0) {
  exit 1
}
