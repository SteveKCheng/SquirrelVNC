# SquirrelVNC, a VNC client for Windows

SquirrelVNC is a fork of TightVNC's viewer with improvements in aesthetics, performance, features and security.

![](art/logo-publish.svg)

## Improvements over TightVNC

  - In “view-only mode”, the screen is prevented from locking / turning off for power-saving.
  - Program is DPI-aware
  - Modernized icons and logo
  - Codebase upgraded to latest compilers and libraries
  - Security fixes \[[details](notes/security.md)\]

## Goals of this project

  - Fast, lightweight VNC viewer
  - Use Windows-specific APIs as appropriate for performance and usability
  - Minimal dependencies other than on a base Windows system
  - Fix security vulnerabilities
  - Compatibility with *modern* VNC servers
  - Improve developers' productivity, and quality on this old codebase
  - Enable AI agents to help with coding and maintenance

## Non-goals for this project

  - Porting to platforms other than Windows
    - There are already good VNC clients out there for those platforms
  - Supporting old versions of Windows that are out-of-support
  - Reviving the server component of TightVNC; it is dropped \[[rationale](notes/old-server.md)\]
  - Supporting every VNC extension under the sun
  - For the author to sell proprietary versions of this software

## Documentation

  - [Command-line arguments](docs/command-line.md)
  
## Building

This software currently only builds on Windows.  You need Visual Studio 2026 installed — any edition is fine.
Make sure you have the following components installed:

  - C++
  - .NET 10 [needed for the [WinDlgGen](notes/dialog-resources.md) tool]

To build on the command line, run:

```
msbuild SquirrelVNC.slnx
```

The following options are available:

  - `/p:Configuration=‹CONFIG›`, where `‹CONFIG›` may be:
    - `Debug`
    - `Release` [default]
    - `ReleaseAsan`
    - `ReleaseFuzz`.
  - `/p:Platform=‹PLATFORM›`, where `‹PLATFORM›` is either: 
    - `Win32` (meaning x86)
    - `x64` [default].

The built outputs will be deposited in the following directories:

  - Generated files for Windows resources: `obj\res`
  - Object files and other intermediate files: `obj\‹PLATFORM›\‹CONFIG›\‹sub-project name›`
  - Final executables: `bin\‹PLATFORM›\‹CONFIG›`

## Repository layout

  - `art/`: artwork for logos and icons
  - `core-lib/`: code factored out of the viewer itself that should be platform-independent
  - `docs/`: user-level documentation
  - `notes/`: technical design notes
  - `harnesses/`: test and fuzz programs
  - `tests/`: test cases (for the RFB protocol)
  - `tools/`: tools used at build-time only
  - `tvnviewer/`: source code for the GUI client itself
  - `win32-lib/`: Windows-specific code factored out of the viewer itself

## Status

Originally the author only needed a small fix to TightVNC, but figured others might be interested, 
so the result has been published to GitHub.

It is, of course, a work in progress.

## Copyright

Most of the code is coprighted by GlavSoft, the original authors of TightVNC, licensed under 
[GPL-2.0](LICENSE.txt).

The author's changes are also placed under GPL-2.0.

