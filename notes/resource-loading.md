# Loading strings from resources

How the viewer loads strings from its string table today, why
`StringTable`'s cache is not needed, and a possible simplification
(2026-10-25). Nothing here is implemented yet.

**Priority: low.** Nothing is broken, and none of this is on a hot path.

The Win32 and `rc.exe` facts below are from memory of their
documentation, not re-checked for this note; check them when doing the
work, especially the `/n` details.

## The code today

- `ResourceLoader::loadString(id, &string)` (win32-lib/util) parses the
  string table by hand. It finds the `RT_STRING` block `id / 16 + 1`
  with `FindResource`, skips `id % 16` length-prefixed UTF-16 entries,
  and copies the next one into a `StringStorage`. It always returns
  `true`, leaving "(Undef)" in the string when the block is missing.
  It does not check the entries against the block's size; the data is
  in our own executable, so that is not a security issue.
- `StringTable::getString(id)` (win32-lib/util) calls `loadString()`
  once per ID, keeps the result in a static
  `std::map<WORD, StringStorage>`, and returns a `const TCHAR *` into
  the cached string. Its fallback text "Requested string from
  StringTable cannot be received" is dead, because `loadString()` never
  fails.

## When strings are loaded

Not when dialogs open: dialog text comes from the `DIALOG` templates,
not the string table. The callers are:

| When | What | Strings |
|---|---|---|
| Startup | Viewer system menu (`ViewerMenu::menuItem`) and tray menu (`ControlTrayIcon`) | about 20, once |
| About and Help dialogs open | version text, licensing label; command-line help text | 3; 1 |
| Error paths | command-line parse errors, failure to start listening, the fatal error in `main`, invalid input in Options or Configuration, failure to open a URL | a few, each on a rare event |
| Commands | Connection Info; Save Session (file filters, save-password question) | a few per click |
| Toolbar tooltips | `ViewerWindow::onNotify`, on each `TTN_NEEDTEXT` | 1, on hovering over a button |

The tooltips, both menus and the Help dialog call
`ResourceLoader::loadString()` directly, bypassing `StringTable`. The cache therefore holds a few
dozen entries at most (`tvnviewer.rc` has about 50 strings), each
looked up in response to a user action, so the choice of container
does not matter for speed.

All callers run on the GUI thread, or on the main thread before the
message loop starts (see `concurrency.md`). The cache has no lock, so
it would become a data race if code on another thread used it; the
password dialog runs on the core thread but does not use
`StringTable` today.

## What the cache is really for: lifetime

Callers keep the returned `const TCHAR *` after the call: they pass it
to an exception, use it as a `format()` string, or pass it to
`MessageBox`. The map works because its nodes never move, so the
pointer stays valid for the life of the process. A replacement
container must keep that: `std::unordered_map` does (rehashing does not
move elements); a `std::vector` indexed by ID does not, unless sized up
front.

## Resources are already in memory

The loader maps the whole executable image, its `.rsrc` section
included, into the process: the pages are read-only, shared, and paged
in from the file on demand. `LoadResource` returns a pointer into that
mapping (the `HGLOBAL` is a pointer in Win32), `LockResource` just
returns it, and `UnlockResource` and `FreeResource` are obsolete no-ops
kept from 16-bit Windows. (`loadString()` still calls both.) So the
string data already lives for the life of the process, and a cache
that copies it into the heap only duplicates it.

`LoadStringW(instance, id, reinterpret_cast<LPWSTR>(&p), 0)`, with a
buffer size of 0, gives exactly that: `p` points at the string inside
the mapped resource, and the return value is its length in characters
(0 if there is no such string). It does the block arithmetic that
`loadString()` does by hand.

The catch: string-table entries are length-prefixed, not
null-terminated, and every caller of `getString()` wants a C string.

## Possible simplification

1. Compile the resources with `rc /n`, which null-terminates the
   strings in string tables: `<NullTerminateStrings>true</NullTerminateStrings>`
   in the `ResourceCompile` item definition in `tvnviewer.vcxproj`
   (shared by all configurations). Check whether the length that
   `LoadStringW` returns then counts the terminator.
2. `StringTable::getString(id)` becomes `LoadStringW` with a buffer size
   of 0, returning `p`, or the fallback text when the length is 0. No
   cache, no allocation, no lock; it is then safe on any thread.
   Assert that the terminator is where `/n` puts it, so that a build
   without `/n` fails loudly in Debug instead of reading past the
   string.
3. Re-implement `ResourceLoader::loadString()` on top of it, or move
   its callers (`ViewerMenu`, `HelpDialog`, the tooltip in
   `ViewerWindow`) to `StringTable` and remove it.

The alternative to `/n` is for `getString()` to return
`std::wstring_view`, which needs no terminator but changes every call
site, since they all pass the string on to APIs that take C strings.

A non-Windows port (`portability.md`) has no resources at all, so the
strings would come from somewhere else there. Returning a pointer to
storage that lives for the whole process keeps that option open.
