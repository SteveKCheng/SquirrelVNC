# Copyright (C) 2026 Steve Cheng.
#
# This file is part of SquirrelVNC, a modified version of the TightVNC
# software (https://www.tightvnc.com/). It is free software under the GNU
# General Public License, version 2 or later; see LICENSE.txt.

<#
.SYNOPSIS
Runs a clang-query file on every source in compile_commands.json, in
parallel, and keeps only the lines the analysis scripts read.

.DESCRIPTION
Run from the top of the tree, after writing compile_commands.json with
tools/clang-tidy/make-compile-commands.ps1 (Debug x64 was used). Uses
the clang-query of the LLVM that Visual Studio installs. Each source
takes a few seconds; all 213 take a few minutes with 8 in parallel.

The analysis scripts expect the paths in the output to start with the
checkout's path, which is hard-coded in them (PRE); change it there for
another checkout.

.EXAMPLE
pwsh tools/pointer-parameters/run-query.ps1 tools/pointer-parameters/find-params.query ..\ptr.out
#>
param(
  [Parameter(Mandatory)] [string] $Query,
  [Parameter(Mandatory)] [string] $Out,
  # Lines of interest: dumped nodes and bindings (dump output), the
  # location and source line of a binding (diag output), and the
  # "N matches." lines that separate the queries in a file.
  [string] $Pattern = '^(Match #|Binding for|[A-Z][A-Za-z]+ 0x)|binds here|^ *[0-9]+ \| |^\d+ match',
  [int] $ThrottleLimit = 8
)
$llvm = (Get-ChildItem "C:\Program Files\Microsoft Visual Studio\*\*\VC\Tools\Llvm\x64\bin\clang-query.exe")[0].FullName
$queryPath = (Resolve-Path $Query).Path
$files = (Get-Content compile_commands.json | ConvertFrom-Json) | ForEach-Object { $_.file } | Sort-Object -Unique
$files | ForEach-Object -Parallel {
  $o = & $using:llvm -p . -f $using:queryPath $_ 2>$null
  ($o | Select-String -Pattern $using:Pattern -Raw) -join "`n"
} -ThrottleLimit $ThrottleLimit | Set-Content $Out
