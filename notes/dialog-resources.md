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

