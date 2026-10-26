# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

<#
.SYNOPSIS
Runs the replay tests in tests/cases with rfb-replay, the unit tests in
unit-test, the fuzz inputs in tests/fuzz with rfb-fuzz, and those in
tests/fuzz-ini with ini-fuzz.

.DESCRIPTION
Each case is one command, rfb-replay --check, which compares the client's
bytes, the event log and the final frame buffer itself and exits with 0
for a match. Each case runs three times: with its input whole, one byte
at a time (--split 1), and in pieces of random sizes (--split 64 --seed 1). The fuzz corpus and regression inputs are one more command,
rfb-fuzz with every file, which runs each once and exits with 0 if none
of them crashed. This script only runs those commands and counts
failures, so that CTest can take over when CMake builds the core
(notes/testing.md).

unit-test is one more command, which runs its own checks and exits with 0
if all of them passed.

A configuration that lacks one of the programs (ReleaseFuzz has no
rfb-replay or unit-test) skips its tests.

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
$unitTest = Join-Path $bin 'unit-test.exe'
$iniFuzz = Join-Path $bin 'ini-fuzz.exe'
if (-not (Test-Path $replay) -and -not (Test-Path $fuzz) -and -not (Test-Path $unitTest)) {
  Write-Error "None of rfb-replay.exe, unit-test.exe and rfb-fuzz.exe is in $bin (build the solution for $Configuration|$Platform first)"
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
  # Each case runs three times: with the input given as the core asks for
  # it; one byte at a time; and in pieces of random sizes up to 64 bytes,
  # from a fixed seed. All must give the same results, which exercises
  # every point where the protocol code can suspend (notes/async-networking.md,
  # Phase 3).
  $splits = @(
    @{ Label = ''; Args = @() },
    @{ Label = ' (1 byte at a time)'; Args = @('--split', '1') },
    @{ Label = ' (random pieces)'; Args = @('--split', '64', '--seed', '1') }
  )
  foreach ($case in $cases) {
    $name = $case.BaseName
    $prefix = Join-Path $case.DirectoryName $name
    foreach ($split in $splits) {
      $total++
      $label = "$name$($split.Label)"
      # The core's log goes to stderr; show it only for failures.
      $output = & $replay --check $prefix --log 1 @($split.Args) 2>&1 | ForEach-Object { "$_" }
      $code = $LASTEXITCODE
      if ($code -eq 0) {
        Write-Host "PASS $label"
      } else {
        $failed += $label
        Write-Host "FAIL $label ($(Get-Reason $code))"
        $output | ForEach-Object { Write-Host "     $_" }
      }
    }
  }
} else {
  Write-Host "SKIP replay cases: rfb-replay is not built in $Configuration"
}

if ((Test-Path $unitTest) -and 'unit-test' -like $Filter) {
  $total++
  $output = & $unitTest 2>&1 | ForEach-Object { "$_" }
  $code = $LASTEXITCODE
  if ($code -eq 0) {
    Write-Host "PASS unit-test"
  } else {
    $failed += 'unit-test'
    Write-Host "FAIL unit-test ($(Get-Reason $code))"
    $output | Where-Object { $_ -notmatch '^PASS ' } | ForEach-Object { Write-Host "     $_" }
  }
} elseif (-not (Test-Path $unitTest)) {
  Write-Host "SKIP unit-test: it is not built in $Configuration"
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
