# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

<#
.SYNOPSIS
Runs clang-tidy on the sources in compile_commands.json, in parallel, and
prints each warning once; with -Fix, applies the fixes it suggests.

.DESCRIPTION
Uses the clang-tidy of the LLVM that Visual Studio installs, with the
environment of vcvars64.bat, so that clang-cl finds the Windows SDK and
the MSVC headers as a build does. Write the database first with
make-compile-commands.ps1.

Warnings in headers are reported once, though each source that includes
the header finds them again.

With -Fix, the fixes of all sources are collected first and then applied
together by clang-apply-replacements, which drops the copies of a fix
that several sources suggest for the same header. It drops them only for
the same path, so the paths are made canonical first: a header reached
by two spellings of its path ("io-lib/Channel.h" and "io-lib\Channel.h")
would otherwise get an inserted text twice. make-compile-commands.ps1
makes the include directories absolute, so that the paths in the fixes
are too. Review the result with
git diff: a fix can leave the code misaligned (removing "virtual " does
not move the lines that continue a declaration).

Requires PowerShell 7. See notes/clang-tidy.md.

.EXAMPLE
pwsh tools/clang-tidy/run-clang-tidy.ps1 -Checks '-*,modernize-use-override'

.EXAMPLE
pwsh tools/clang-tidy/run-clang-tidy.ps1 -Checks '-*,modernize-use-nullptr' -Filter '*\win32-lib\*' -Fix
#>
param(
  # clang-tidy's --checks; empty uses a .clang-tidy file, if one is added.
  [string]$Checks = '',
  # Run only on the sources whose full paths match this wildcard.
  [string]$Filter = '*',
  # Apply the suggested fixes.
  [switch]$Fix,
  [int]$Jobs = [Environment]::ProcessorCount,
  # The directory of compile_commands.json.
  [string]$Database = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Database) {
  $Database = $root
}
$databaseFile = Join-Path $Database 'compile_commands.json'
if (-not (Test-Path $databaseFile)) {
  Write-Error "No $databaseFile (run make-compile-commands.ps1 first)"
}

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$llvm = Join-Path $vs 'VC\Tools\Llvm\x64\bin'
$tidy = Join-Path $llvm 'clang-tidy.exe'
$apply = Join-Path $llvm 'clang-apply-replacements.exe'

# The include paths of the Windows SDK and MSVC, which clang-cl takes from
# INCLUDE as cl does.
cmd /c "`"$vs\VC\Auxiliary\Build\vcvars64.bat`" >nul && set" | ForEach-Object {
  if ($_ -match '^([^=]+)=(.*)$') {
    Set-Item -Path "env:$($matches[1])" -Value $matches[2]
  }
}

# A misspelt check enables nothing, which would look like a clean result.
$listArguments = @('--list-checks')
if ($Checks) {
  $listArguments += "--checks=$Checks"
}
$enabled = @(& $tidy @listArguments 2>&1 | Where-Object { "$_" -match '^\s+\S' })
if ($enabled.Count -eq 0) {
  Write-Error "No check is enabled by '$Checks'"
}
"$($enabled.Count) checks enabled"

$files = @((Get-Content $databaseFile | ConvertFrom-Json).file |
           Sort-Object -Unique | Where-Object { $_ -like $Filter })
# Warnings in our own headers, not in those of Windows, MSVC or vcpkg.
$headerFilter = '.*[/\\](core-lib|win32-lib|tvnviewer|harnesses|tools)[/\\].*'
$fixes = Join-Path ([IO.Path]::GetTempPath()) "squirrel-tidy-fixes-$PID"
New-Item -ItemType Directory $fixes | Out-Null

try {
  $output = $files | ForEach-Object -ThrottleLimit $Jobs -Parallel {
    $arguments = @('-p', $using:Database, "--header-filter=$using:headerFilter", '--quiet')
    if ($using:Checks) {
      $arguments += "--checks=$using:Checks"
    }
    if ($using:Fix) {
      $name = [IO.Path]::GetFileNameWithoutExtension($_) + '-' + [Guid]::NewGuid()
      $arguments += "--export-fixes=$using:fixes\$name.yaml"
    }
    & $using:tidy @arguments $_ 2>&1 | ForEach-Object { "$_" }
  }

  # A diagnostic and the lines that show its source, up to the next one;
  # the same one is printed once, whichever spelling of its path.
  $seen = @{}
  $current = $null
  $block = @()
  foreach ($line in @($output) + 'end:') {
    if ($line -match ': (warning|error|note): ' -or $line -eq 'end:') {
      $key = if ($current) { $current.Replace('/', '\') } else { $null }
      if ($key -and -not $seen.ContainsKey($key)) {
        $seen[$key] = $true
        $block
      }
      $current = $line
      $block = @($line)
    } elseif ($current -and $line -notmatch '^\d+ warnings? (and \d+ errors? )?generated') {
      $block += $line
    }
  }
  "$($files.Count) sources checked; $(@($seen.Keys | Where-Object { $_ -match ': (warning|error): ' }).Count) distinct warnings and errors"

  if ($Fix) {
    # Canonical paths, so that clang-apply-replacements sees one file.
    foreach ($yaml in Get-ChildItem $fixes -Filter *.yaml) {
      $text = Get-Content -Raw $yaml.FullName
      $text = [regex]::Replace($text, "(FilePath:\s*)'((?:[^']|'')*)'", {
        param($m)
        # Absolute already (see make-compile-commands.ps1); made canonical.
        $path = [IO.Path]::GetFullPath($m.Groups[2].Value.Replace("''", "'"))
        $m.Groups[1].Value + "'" + $path.Replace("'", "''") + "'"
      })
      Set-Content -NoNewline -Path $yaml.FullName -Value $text
    }
    & $apply $fixes
    if ($LASTEXITCODE -ne 0) {
      Write-Error "clang-apply-replacements failed"
    }
    "Fixes applied; review them with git diff"
  }
} finally {
  Remove-Item -Recurse -Force $fixes
}
