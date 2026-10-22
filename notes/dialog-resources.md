# Win32 dialog resources in the GUI program

Editing the dialog resources every time we need to expose a new option or change
how an existing option is exposed is very tedious.  The resource editor in the
Visual Studio IDE does not make it easy.

We use a tool, [WinDlgGen](https://github.com/SteveKCheng/WinDlgGen/) to generate
the dialog resources from a more abstract description in XML, using a box model 
reminiscent of CSS.

The tool is imported as a Git submodule in `tools/WinDlgGen`.  To build this software,
you must have this submodule checked out *and* have .NET 10 installed so the tool 
can be built from source.  

The build rules for `tvnviewer` will automatically build and invoke the tool as
necessary.

The input XML files are in [`tvnviewer/res/*.xml`](../tvnviewer/res/).  See the 
documentation for WinDlgGen for the description of the XML format, and what WinDlgGen
does with them.


## How the build uses it

In `tvnviewer/tvnviewer.vcxproj`:

- Target `EnsureWinDlgGen` restores the tool's NuGet packages, then builds
  it in Release configuration, to
  `tools/WinDlgGen/out/bin/WinDlgGen/release/WinDlgGen.exe`. The restore
  matters: without it, a command-line build fails unless the IDE has
  already restored the packages for the same Windows user. This tree is
  built by more than one user, and the tool's `out/obj/project.assets.json`
  names the NuGet cache of whoever restored it last.
- Target `GenerateDialogResources` runs before `ResourceCompile`. It passes
  every `<WinDlgGen Include="res\...xml" />` item to the tool in one
  invocation, with `-i res\preamble.h`, and writes
  `obj/res/WinDlgGenOutput.rc`.
- That output is compiled as a `ResourceCompile` item of its own, alongside
  `res/tvnviewer.rc`. Its path does not depend on the Configuration or
  Platform, so it can be listed in Solution Explorer, and the resource
  editor can show the generated dialogs.
- `res/preamble.h` includes `resource.h` and `winres.h`, which are what the
  output needs. `winres.h` defines `IDC_STATIC` and the common controls'
  style flags used so far, such as `TBS_AUTOTICKS`.

If `tools/WinDlgGen` is empty, the build stops with an error saying the
submodule is not checked out: run `git submodule update --init`.

## Moving a dialog from `tvnviewer.rc` to XML

The commits for `IDD_OPTIONS` and `IDD_DAUTH` show the pattern:

1. Write `tvnviewer/res/<name>-dialog.xml`, with the same identifiers as
   the `DIALOGEX` it replaces, so that the C++ code does not change.
   WinDlgGen does not define identifiers; they stay in `res/resource.h`.
2. Add it as a `<WinDlgGen Include="res\<name>-dialog.xml" />` item in
   `tvnviewer.vcxproj`.
3. Delete the dialog's `DIALOGEX` block from `res/tvnviewer.rc`. Its entry
   in the `DESIGNINFO` block has been left in place so far. It is harmless:
   only the resource editor's guide lines use it.

Things to expect and check:

- **Widths of text.** WinDlgGen does not measure text, so labels next to
  other controls in a row need a `width`. The widths in the old `DIALOGEX`
  are the best starting point, since they were sized by eye for the dialog
  font. Labels and check boxes stacked vertically need none: they stretch
  to the width of their container.
- **Sizes change.** The dialog's overall size comes out a little
  different, and controls get the standard sizes: edit boxes and combo
  boxes 14 high, buttons 50 by 14, labels 8 high. Say so in the commit
  message.
- **Styles change.** The generated dialog style drops `DS_3DLOOK`, which
  does nothing for Win32 programs, and `WS_VISIBLE`, which a modal dialog
  does not need. A dialog created modeless with `CreateDialog` and expected
  to show itself would need `style="... | WS_VISIBLE"`; see WinDlgGen's
  `docs/format.md`. Controls also get `WS_GROUP` and `WS_TABSTOP` by
  WinDlgGen's rules, which can differ from the hand-made dialog. For
  example, the old `IDD_OPTIONS` radio buttons had no tab stop.
- **Errors.** Mistakes in the XML fail the build with
  `file(line,col): error WDGnnnn: ...` lines, which the IDE's Error List
  shows.

## Checking the result without a desktop

An agent working over SSH has no desktop. It must not run `SquirrelVNC.exe`,
because a dialog or message box would wait forever for input. Instead:

- Read `obj/res/WinDlgGenOutput.rc` for the generated coordinates and
  styles.
- Check that a dialog made it into the program by loading the exe as a
  data file and looking up the dialog resource (type 5, `RT_DIALOG`), from
  PowerShell:

  ```powershell
  Add-Type -Name K -Namespace W -MemberDefinition @'
  [DllImport("kernel32", CharSet=CharSet.Unicode)] public static extern IntPtr LoadLibraryExW(string p, IntPtr h, uint f);
  [DllImport("kernel32")] public static extern IntPtr FindResourceW(IntPtr m, IntPtr n, IntPtr t);
  [DllImport("kernel32")] public static extern uint SizeofResource(IntPtr m, IntPtr r);
  '@
  # 2 = LOAD_LIBRARY_AS_DATAFILE; 131 = IDD_DAUTH in res/resource.h
  $m = [W.K]::LoadLibraryExW("$PWD\bin\x64\Release\SquirrelVNC.exe", 0, 2)
  [W.K]::SizeofResource($m, [W.K]::FindResourceW($m, [IntPtr]131, [IntPtr]5))
  ```

  A size of 0 means the dialog is missing.
- How the dialog looks has to be checked by a person, in the resource
  editor or by running the program.

## Previewing the dialogs in Visual Studio

Currently, WinDlgGen is not able to show the generated dialogs visually. 
But they can be readily be seen in the Visual Studio IDE.  After building,
simply open `WinDlgGenOutput.rc` in the Solution Explorer pane in the IDE.
