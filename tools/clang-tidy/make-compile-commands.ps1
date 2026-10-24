# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

<#
.SYNOPSIS
Writes compile_commands.json, the JSON compilation database that
clang-tidy and clangd read, from the Visual Studio projects.

.DESCRIPTION
Runs MSBuild's GetClCommandLines target on each C++ project (through
CompileCommands.targets), which gives the compiler options exactly as a
build in that configuration would use them, include directories from
vcpkg and defines from the project settings included. Nothing is built.
Each source becomes one entry for clang-cl, the MSVC-compatible driver of
the LLVM that Visual Studio installs.

Only needed until CMake builds the code, which writes the database
itself (CMAKE_EXPORT_COMPILE_COMMANDS). See notes/clang-tidy.md.

.EXAMPLE
pwsh tools/clang-tidy/make-compile-commands.ps1 -Configuration Debug -Platform x64
#>
param(
  [string]$Configuration = 'Debug',
  [string]$Platform = 'x64',
  # Where to write the database; clang-tidy looks for it in the
  # directories above the sources, so the default is the top directory.
  [string]$Output = ''
)

$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent (Split-Path -Parent $PSScriptRoot)
if (-not $Output) {
  $Output = Join-Path $root 'compile_commands.json'
}

$vs = & "${env:ProgramFiles(x86)}\Microsoft Visual Studio\Installer\vswhere.exe" -latest -property installationPath
$msbuild = Join-Path $vs 'MSBuild\Current\Bin\MSBuild.exe'
$clangCl = Join-Path $vs 'VC\Tools\Llvm\x64\bin\clang-cl.exe'
if (-not (Test-Path $clangCl)) {
  Write-Error "No clang-cl.exe at ${clangCl}: install the C++ Clang tools for Windows with the Visual Studio installer"
}
$targets = Join-Path $PSScriptRoot 'CompileCommands.targets'

# clang-cl targets the machine it runs on unless told otherwise; cl's
# options do not say, since each cl.exe has one target.
$target = if ($Platform -eq 'Win32') { '--target=i686-pc-windows-msvc' } else { '' }

$projects = git -C $root ls-files '*.vcxproj' | ForEach-Object { Join-Path $root $_ }
$dump = New-TemporaryFile
try {
  foreach ($project in $projects) {
    & $msbuild $project /t:SquirrelCompileCommands /nologo /v:quiet `
      "/p:Configuration=$Configuration" "/p:Platform=$Platform" `
      "/p:ForceImportAfterCppTargets=$targets" "/p:SquirrelCompileCommandsFile=$dump"
    if ($LASTEXITCODE -ne 0) {
      Write-Error "MSBuild failed on $project"
    }
  }

  $entries = foreach ($line in Get-Content $dump) {
    $options, $files, $directory = $line -split "`t"
    # The object file and error reporting mean nothing to clang-tidy.
    $options = $options -replace '/Fo"[^"]*"', '' -replace '/errorReport:\w+', ''
    # Include directories made absolute and canonical: clang-tidy reports
    # a header by the path it was found by, and its fixes would otherwise
    # name it relative to each project's directory, by several spellings.
    $options = [regex]::Replace($options, '/I(?:"([^"]*)"|(\S+))', {
      param($m)
      $path = if ($m.Groups[1].Success) { $m.Groups[1].Value } else { $m.Groups[2].Value }
      '/I"' + [IO.Path]::GetFullPath($path, $directory) + '"'
    })
    foreach ($file in $files -split ';') {
      [ordered]@{
        directory = $directory
        file = $file
        command = "`"$clangCl`" $target $options `"$file`""
      }
    }
  }
} finally {
  Remove-Item $dump
}

# The same source in two projects (none now) would get two entries;
# clang-tidy takes the first.
ConvertTo-Json -InputObject @($entries) -Depth 3 | Set-Content -Encoding utf8NoBOM $Output
"$(@($entries).Count) sources of $(@($projects).Count) projects, for $Configuration|$Platform, in $Output"
