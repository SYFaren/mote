# mote

Tiny text editor with an **ANSI C89 core** and compile-time platform overlays.
Current maintained builds are the POSIX terminal (verified on Linux) and
Windows console; the overlay design can support additional backends.

[Русская версия](README.ru.md)

- [Quick start](#quick-start)
- [Features](#features)
- [Keys](#keys)
- [Command line and settings](#command-line-and-settings)
- [Platforms](#platforms)
- [Layout and release](#layout-and-release)

## Quick start

```sh
make                 # POSIX terminal
make windows         # Windows console (MinGW-w64)
make test && make smoke
```

The maintained builds are POSIX terminal (`make posix`) and Windows console
(`make windows`). The shared core uses platform overlays, so additional backends
can be added later. See [`docs/BUILD.md`](docs/BUILD.md).

## Features

### Files and documents

- Up to **6 documents** open at once; the status bar shows `[2/4]`.
- **Open** a path (`Ctrl+O`). A missing file opens as a new empty one. A clean
  document is replaced; a modified one keeps its tab and the file opens in a
  new tab.
- **Quick open** (`Ctrl+P`): files in the current file's folder (up to 256,
  sorted, dotfiles skipped) with a fuzzy filter — `edc` finds `editor.c`.
  Up/Down to pick, Enter to open.
- **Recent files** (`Ctrl+E`): the last 8, kept between runs. Pick with 1–8,
  Up/Down or j/k.
- **Reload** from disk (`F5`); refused while there are unsaved edits.
- **Safe save**: written to a temporary file, flushed with fsync, then renamed
  over the original, so a failed save never destroys the old file. An untitled
  document asks for a name.
- **Crash recovery**: after each changed input batch, dirty documents are written
  to a separate snapshot next to the config (`config.recovery-N`, slots 0–15).
  On the next launch: `Enter` restores, `Ctrl+Q` discards, `Esc` exits and keeps
  the snapshot. Original files are never overwritten by restoration; review and
  save explicitly. Untitled documents and LF/CRLF mode are included; undo history
  and caret positions are not. Relative paths restore as untitled to avoid saving
  into the wrong working directory. Recovery takes precedence over CLI files.
  Normal exit removes that instance's snapshot. Concurrent instances use locked
  slots; if no slot is available, recovery is disabled with a status warning.
  Snapshot/write failures retain the previous copy and show a warning. The latest
  input batch may still be lost in a crash; this is not a guarantee against power
  loss. Whole-document snapshots can slow editing of large files. Copies contain
  plaintext (POSIX permissions `0600`; Windows inherits the config directory ACL).
- **Unsaved-changes question** on quit, close or open: `Ctrl+S` save,
  `Ctrl+Q` discard, `Esc` cancel. Quitting saves every modified document and
  asks for names of untitled ones first.

### Text, encodings and line ends

- UTF-8 throughout: caret moves and deletes whole characters (Cyrillic, emoji).
- A UTF-8 BOM is dropped on load.
- A file with non-ASCII bytes that is not valid UTF-8 requires confirmation:
  `Enter` interprets it as Windows-1251 and converts it to UTF-8; `Esc` cancels
  without replacing the current document. This is an explicit choice, not
  encoding detection: convert Latin-1 or other encodings externally first.
  Saving writes UTF-8, not CP1251. Files containing ASCII control bytes other
  than Tab, LF and CR (including NUL and DEL) are refused as binary/control data.
- **LF / CRLF** detected on load and kept on save; a lone CR becomes LF.
  `Alt+E` toggles the line ending.
- File size limit: 64 MiB per document. There is no aggregate limit across the
  six tabs; the gap buffer reserves roughly the file size, and undo stores edit
  data separately, so peak memory can be substantially higher than the file.

### Editing

- Undo / redo, up to 512 edit operations per document; these store inserted or
  deleted text (deltas), not full-document snapshots. Consecutive typing is one
  step, and large edits can still use substantial memory.
- **Auto-close** of `(` `[` `{` `"` `'`; with a selection the pair wraps it.
- **Auto-indent**: Enter keeps the current line's indentation.
- **Tab** inserts a tab (width 4) or indents every selected line;
  **Shift+Tab** outdents (one tab or up to 4 spaces).
- Duplicate line, delete line, cut / copy / paste, select all.
- **Toggle comment** on the line or every selected line, in the language's own
  style:

  | Language | Comment |
  |---|---|
  | Python, shell, YAML, Makefile | `# text` |
  | SQL | `-- text` |
  | CSS | `/* text */` |
  | HTML / XML, Markdown | `<!-- text -->` |
  | everything else | `// text` |

  If every non-blank line is already commented, the comment is removed.
- **Bracket match**: the pair around the caret is highlighted; `Ctrl+]` jumps
  to the partner.
- **Read-only** mode (`Alt+R`) blocks every edit, undo included; `RO` in the
  status bar.

### Navigation and selection

- Arrows, Home / End, PgUp / PgDn; with Shift they extend the selection.
- `Ctrl+Left` / `Ctrl+Right` move by words.
- Up / Down keep the column, also across wrapped lines.
- **Go to line** (`Ctrl+G`).

### Find and replace

- **Find** (`Ctrl+F`): plain text, case-insensitive by default;
  `Alt+C` match case, `Alt+W` whole word (both remembered).
- All visible matches are highlighted; the current match is selected separately.
- `Esc` clears search highlighting but keeps the query; `F3` resumes the search.
- **Regex**: type `/pattern/`. Supports `.` `*` `+` `?`, `^` `$` (line start
  and end), `[...]` `[^...]` with ranges, `\d` `\w` `\s` and escapes. No groups
  or alternation. To find a literal `/`, escape it as `\/`: for example,
  `/a\/b/` matches `a/b`. Replacement text is literal, not regex-unescaped.
- `F3` / `Shift+F3`: next / previous match, wrapping around the file.
- **Replace** (`Ctrl+R`) replaces every match and reports the count. Plain text
  is the replacement for the last search; `/find/replace/` sets both as a regex.

### Bookmarks

- Up to 4 per document, marked `*` next to the line number.
- `F8` toggles one on the caret line; `F9` jumps to the **nearest** one (ties go
  down).
- Bookmarks follow their lines through edits above them.

### View

- Line numbers, current-line highlight, status bar with file name, `*` when
  modified, `RO`, line:column, LF/CRLF, language and the last message.
- **Word wrap** (`Ctrl+W`), visual only; without it the view scrolls
  horizontally.
- **Show whitespace** (`F7`): `.` and `>` depending on the terminal.
- **Themes** (`Ctrl+T`): dark, light, slate.
- **Zoom** (`Ctrl+=` / `Ctrl+-` / `Ctrl+0`), 8–48 px, default 15. This is a
  platform hook for GUI ports; the current terminal ports cannot change the
  terminal font. Legacy terminal keyboard protocols often cannot report
  `Ctrl+=`, so that shortcut may not reach the app.
- **Help** (`F1`): one or two columns depending on width, scrollable.

### Syntax highlighting

C/C++, Python, JS/TS, Go, Rust, Java, shell, SQL, PHP, JSON, HTML/XML (incl.
SVG, plist), CSS/SCSS, Markdown, YAML and Makefiles, chosen by file name.
Keywords, types and constants, strings (incl. Python triple quotes), numbers,
line and block comments and preprocessor lines are colored; a block comment
that starts above the screen is still tracked.

## Keys

`Ctrl+Shift+letter` is not distinguishable from `Ctrl+letter` in most
terminals, so those actions also have an `Alt` key.

The POSIX console disables terminal software flow control while running, so
`Ctrl+S` and `Ctrl+Q` normally reach the editor. Multiplexers or remote terminal
setups may still intercept them; if a shortcut is swallowed, adjust the
terminal/multiplexer configuration to release it.

### File

| Key | Action |
|---|---|
| `Ctrl+S` | save |
| `Alt+S`, `Ctrl+Shift+S` | save as |
| `Ctrl+O` | open |
| `Ctrl+P` | quick open |
| `Ctrl+E` | recent files |
| `Ctrl+N` | new document |
| `F5` | reload from disk |
| `Ctrl+Tab`, `F2`, `Alt+N` | next document (Shift / `Alt+P`: previous) |
| `Ctrl+F4`, `Ctrl+Shift+W` | close document |
| `Ctrl+Q` | quit |

### Edit

| Key | Action |
|---|---|
| `Ctrl+Z` / `Ctrl+Y` | undo / redo |
| `Ctrl+X` / `Ctrl+C` / `Ctrl+V` | cut / copy / paste |
| `Ctrl+A` | select all |
| `Ctrl+D` | duplicate line |
| `Alt+K`, `Ctrl+Shift+K` | delete line |
| `Ctrl+/` | toggle comment |
| `Tab` / `Shift+Tab` | indent / outdent |
| `Ctrl+]` | jump to matching bracket |

### Find and navigate

| Key | Action |
|---|---|
| `Ctrl+F` | find (`/re/` = regex) |
| `F3` / `Shift+F3` | next / previous match |
| `Ctrl+R` | replace (`/find/repl/` = regex) |
| `Alt+C` / `Alt+W` | match case / whole word |
| `Ctrl+G` | go to line |
| `F8`, `Alt+M`, `Ctrl+Shift+M` | toggle bookmark |
| `F9`, `Alt+J`, `Ctrl+J`, `Ctrl+B` | jump to nearest bookmark |

### View

| Key | Action |
|---|---|
| `Ctrl+W` | word wrap |
| `F7` | show whitespace |
| `Ctrl+T` | next theme |
| `Ctrl+=` / `Ctrl+-` / `Ctrl+0` | zoom in / out / reset |
| `Alt+R`, `Ctrl+Shift+R` | read-only |
| `Alt+E`, `Ctrl+Shift+E` | LF / CRLF |
| `F1`, `Ctrl+H`, `Alt+H` | help (also F10–F12 in a terminal) |

## Command line and settings

```text
mote [-h] [-v] [-H] [-g WxH] [file ...]
  -h, --help         show help
  -v, --version      print version
  -H, --start-help   open the key help on start
  -g, --geometry     COLSxROWS (40..300 x 10..120)
  file ...           open up to 6 files
```

Saved on exit: theme, font size, word wrap, whitespace, find
options and recent files.

The config is a plain text `key=value` file. For example:

```text
theme=0
font_px=15
wrap=1
show_ws=0
find_case=0
find_word=0
```

Unknown keys are ignored. `-g WxH` sets the terminal grid size (columns × rows)
for these console ports; it does not resize the terminal window, so the host
terminal may resize or clip the displayed grid.

| System | Config file |
|---|---|
| Linux | `~/.config/mote/config` (or `$XDG_CONFIG_HOME/mote/config`) |
| Windows | `%APPDATA%\mote\config` |

Environment variables:

| Variable | Effect |
|---|---|
| `MOTE_START_HELP` | open help on start |
| `MOTE_TRUECOLOR`, `MOTE_NO_TRUECOLOR` | POSIX terminal: force or forbid 24-bit color |
| `MOTE_UTF8`, `MOTE_NO_UTF8` | POSIX terminal: force or forbid UTF-8 output |
| `MOTE_VT`, `MOTE_NO_VT` | Windows console: VT output on / off |
| `MOTE_DUMP_CELLS`, `MOTE_SHOT_ONCE` | Windows console cell-grid screenshots for tests |

## Platforms

| Port | Runs on | Mouse | System clipboard |
|---|---|---|---|
| `console` | POSIX terminal (Linux tested) | no | paste via `wl-paste` / `xclip` |
| `winconsole` | Windows console | no | yes |

The POSIX terminal port picks truecolor, 256 or 16 colors by itself and accepts
bracketed paste. Shortcuts also work on non-Latin keyboard layouts. The current
maintainer build/release focus is POSIX and Windows console; the shared core and
overlay design remain suitable for additional backends.

## Layout and release

```text
core/
  app/      application lifecycle and command-line handling
  document/ gap buffer and undo history
  editor/   editor state, input and rendering
  syntax/   syntax highlighting and regex matcher
  utils/    UTF-8, formatting, themes and shared definitions
  system/   config, key mapping and directory listing
plat/       platform.h (overlay contract)
overlay/    console (POSIX) · winconsole (Windows)
docs/       build and release guidance
test/       unit, editor-key, terminal-decoder and smoke checks
```

`core/` must stay C89 (`make ansi-check`); each overlay adds a platform layer.
See [`overlay/README.md`](overlay/README.md).

```sh
make release
```
