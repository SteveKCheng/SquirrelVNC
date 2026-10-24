# Running clang-tidy

clang-tidy checks C++ code for bugs, style and modernisation, and can fix
much of what it finds. It was used on the `cpp-clean-up` branch for
`4f4ed7f` (`override` instead of `virtual`, with modernize-use-override).

clang-tidy needs a JSON compilation database (`compile_commands.json`):
the compiler command for each source. CMake writes one
(`CMAKE_EXPORT_COMPILE_COMMANDS`); until the code builds with CMake,
`tools/clang-tidy/make-compile-commands.ps1` makes one from the Visual
Studio projects.

## Requirements

- The "C++ Clang tools for Windows" component of Visual Studio
  (Visual Studio Installer, Individual components). It installs LLVM in
  `VC\Tools\Llvm\x64\bin`: `clang-cl.exe`, `clang-tidy.exe` and
  `clang-apply-replacements.exe` (LLVM 22.1.3 with Visual Studio 18).
- PowerShell 7 (`pwsh`), for the parallel runs.
- vcpkg's packages installed (`vcpkg_installed`), as for a build: the
  database refers to their headers. A build of the configuration does
  it.

## 1. Make the database

```
pwsh tools/clang-tidy/make-compile-commands.ps1 -Configuration Debug -Platform x64
```

This writes `compile_commands.json` at the top of the tree (ignored by
git), where clang-tidy looks for it, and clangd too.

How: for each C++ project tracked by git, MSBuild runs the
`GetClCommandLines` target, which Visual Studio uses for IntelliSense,
through `CompileCommands.targets`, imported with
`ForceImportAfterCppTargets`. It gives the `cl` options exactly as a
build of that configuration would use them, vcpkg's include directory
and the defines of the character set included, without building
anything. Each source becomes an entry for `clang-cl`, the
MSVC-compatible driver, with:

- the include directories made absolute, so that the paths clang-tidy
  reports, and puts in its fixes, are absolute too (see below);
- `/Fo` and `/errorReport` dropped;
- `--target=i686-pc-windows-msvc` for `-Platform Win32`: without it,
  clang-cl analyses for the machine it runs on, with 64-bit pointers.

Make it again after adding or removing sources, or changing project
settings. A configuration's defines decide what is analysed: the code
under `SQUIRREL_LIBFUZZER` is seen only with `-Configuration
ReleaseFuzz`, and `_DEBUG` code only in Debug.

## 2. Run clang-tidy

```
pwsh tools/clang-tidy/run-clang-tidy.ps1 -Checks '-*,modernize-use-nullptr'
pwsh tools/clang-tidy/run-clang-tidy.ps1 -Checks '-*,bugprone-*' -Filter '*\core-lib\*'
pwsh tools/clang-tidy/run-clang-tidy.ps1 -Checks '-*,modernize-use-override' -Fix
```

- `-Checks` is clang-tidy's `--checks`: `-*` first turns off the
  default checks, then a list of checks or globs. The script stops if
  the list enables no check, since a misspelt check would otherwise look
  like a clean result. `clang-tidy --list-checks --checks='*'` lists
  them all.
- `-Filter` restricts the run to the sources whose full paths match a
  wildcard.
- Warnings in headers are reported for our own directories only (not
  for the Windows SDK, MSVC or vcpkg), and each warning is printed once,
  though every source that includes a header finds it again.
- The script runs one clang-tidy per source, as many at a time as there
  are processors (`-Jobs`), with the environment of `vcvars64.bat`, from
  which clang-cl takes the Windows SDK and MSVC include paths (`INCLUDE`)
  as `cl` does.

### Fixes

With `-Fix`, the fixes of all the sources are exported, then applied
together by `clang-apply-replacements`, which drops the copies of a fix
that several sources suggest for the same header. It only does so for
the same path, so the script makes the paths in the fixes canonical
first. Without that, a header reached by two spellings of its path
(`io-lib/IOException.h` from one include, `io-lib\IOException.h` from
another) got an inserted `override` twice: that happened once, and had
to be collapsed by hand.

Then review the change with `git diff`, and build. Things to look for:

- Alignment: removing `virtual ` does not move the lines that continue a
  declaration, and inserted text can make a line too long. There is no
  `.clang-format` to reformat with, and running clang-format on whole
  files would change far more than the fix.
- A fix that compiles but changes behaviour. Most modernize checks are
  safe; some bugprone and readability fixes are only suggestions.

To try the fixing on a file without keeping anything, check that the
file has no other changes, run with `-Filter` on it, and
`git checkout -- <file>` afterwards.

## Other ways

- Visual Studio can run clang-tidy in its code analysis (project
  properties, Code Analysis, Clang-Tidy), and show the warnings in the
  error list, but does not apply fixes.
- With the database at the top of the tree, clangd (for example in
  VS Code) gives completion and clang-tidy's warnings while editing.

## Later

- A `.clang-tidy` file at the top could fix the checks that the code is
  kept clean of; `run-clang-tidy.ps1` uses it when `-Checks` is empty.
- With CMake, the database comes from CMake and `make-compile-commands.ps1`
  and `CompileCommands.targets` can go; `run-clang-tidy.ps1` works on any
  database (`-Database`).
