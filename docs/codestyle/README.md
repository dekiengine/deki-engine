# Deki code style

[CodeStyle.h](CodeStyle.h) and [CodeStyle.cpp](CodeStyle.cpp) show every C++
rule, each next to code that follows it. Read them before writing code for the
engine, the editor or a package. They compile as part of the editor's tests,
so they always build.

## Tools

Both tools come from LLVM 22. On Windows, install them into MSYS2:

```
pacman -S mingw-w64-x86_64-clang-tools-extra
```

- `.clang-format` sets the layout. Format a file with `clang-format -i <file>`.
- `.clang-tidy` checks the names. Check a file with
  `clang-tidy <file> -- -std=c++20 <include flags>`.

The engine holds the original of both files. The editor and every package keep
a copy at their root. Change the engine's, then copy it.

To format your C++ files on every commit, turn on the hook in each repository:

```
git config core.hooksPath <path to deki-engine>/tools/hooks
```

To check that a change touched comments only (a comment clean-up, say),
run `python tools/comment_guard.py <files>` from the engine: it compares each
file with its committed version without comments and fails on anything else.

## Commits and pull requests

A brief title, a blank line, then one bullet per change:

```
Added new platform ESP32 C3

- Fixed problems with the SPI bus
- Added feature to choose the flash size
- Refactored the board list
```

Pull requests use the same form: the title, then the bullets.
