# mote

Tiny multi-platform text editor: an **ANSI C89 core** plus one compile-time
**overlay** per platform. The same editor runs as an X11, Wayland, SDL or Win32
window, in a terminal, in the Windows console, on the Linux framebuffer, under
DOS and in the browser.

[Русская версия](README.ru.md)

- [Quick start](#quick-start)
- [Features](#features)
- [Keys](#keys)
- [Command line and settings](#command-line-and-settings)
- [Platforms](#platforms)
- [Layout and release](#layout-and-release)

## Quick start

```sh
make                 # Linux X11
make console         # Unix terminal
make test && make smoke
```

Other ports: `make wayland`, `make sdl`, `make sdl3`, `make fbdev`, `make win32`,
`make winconsole`, `make dos`, `make wasm`. See [`docs/BUILD.md`](docs/BUILD.md).

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
- **Unsaved-changes question** on quit, close or open: `Ctrl+S` save,
  `Ctrl+Q` discard, `Esc` cancel. Quitting saves every modified document and
  asks for names of untitled ones first.

### Text, encodings and line ends

- UTF-8 throughout: caret moves and deletes whole characters (Cyrillic, emoji).
- A UTF-8 BOM is dropped on load.
- **CP1251** (Windows Cyrillic) files are detected and converted to UTF-8.
- **LF / CRLF** detected on load and kept on save; a lone CR becomes LF.
  `Alt+E` toggles the line ending.
- File size limit: 64 MB (2 MB on DOS).

### Editing

- Undo / redo, 512 steps per document; consecutive typing is one step.
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
- **Mouse** in window ports: click places the caret, Shift+click and drag
  select, the wheel scrolls.

### Find and replace

- **Find** (`Ctrl+F`): plain text, case-insensitive by default;
  `Alt+C` match case, `Alt+W` whole word (both remembered).
- **Regex**: type `/pattern/`. Supports `.` `*` `+` `?`, `^` `$` (line start
  and end), `[...]` `[^...]` with ranges, `\d` `\w` `\s` and escapes. No groups
  or alternation.
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
- **Show whitespace** (`F7`): `·` and `»` in windows, `.` and `>` in text mode.
- **Themes** (`Ctrl+T`): dark, light, slate.
- **Zoom** (`Ctrl+=` / `Ctrl+-` / `Ctrl+0`), 8–48 px, default 15. In text-mode
  ports the terminal owns the font.
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
  -g, --geometry     window size in pixels (min 200x120),
                     or COLSxROWS in text-mode ports
  file ...           open up to 6 files
```

Saved on exit: window size, theme, font size, word wrap, whitespace, find
options and recent files.

| System | Config file |
|---|---|
| Linux, BSD, macOS | `~/.config/mote/config` (or `$XDG_CONFIG_HOME/mote/config`) |
| Windows | `%APPDATA%\mote\config` |
| DOS | `MOTE\CONFIG` |

Environment variables:

| Variable | Effect |
|---|---|
| `MOTE_START_HELP` | open help on start |
| `MOTE_TRUECOLOR`, `MOTE_NO_TRUECOLOR` | terminal: force or forbid 24-bit color |
| `MOTE_UTF8`, `MOTE_NO_UTF8` | terminal: force or forbid UTF-8 output |
| `MOTE_FB` | fbdev: framebuffer device |
| `MOTE_VT`, `MOTE_NO_VT` | Windows console: VT output on / off |
| `MOTE_KEYTRACE` | DOS: log keys to `KEYTRACE.LOG` |
| `MOTE_DUMP_FB`, `MOTE_DUMP_CELLS`, `MOTE_SHOT_ONCE` | screenshots for tests |

## Platforms

| Port | Runs on | Mouse | System clipboard |
|---|---|---|---|
| `x11` | Linux, BSD | yes | yes |
| `wayland` | Linux | yes | no (internal) |
| `sdl` / `sdl3` | Linux, BSD, macOS | yes | yes |
| `wasm` | browser | yes | yes |
| `win32` | Windows window | yes | yes |
| `winconsole` | Windows console | no | yes |
| `console` | Linux, BSD, macOS terminal | no | paste via `wl-paste` / `xclip` |
| `fbdev` | Linux framebuffer | no | no (internal) |
| `dos` | FreeDOS, DOSBox | no | no (internal) |

The terminal port picks truecolor, 256 or 16 colors by itself and accepts
bracketed paste. Shortcuts also work on non-Latin keyboard layouts.

Release builds: Linux amd64 / arm64 / armhf / i686 / riscv64 (incl. static
musl), FreeBSD, OpenBSD, macOS amd64 / arm64, Windows amd64 / i686, DOS and the
web.

## Layout and release

```text
core/       buffer, editor, keymap, utf8, undo, hl, regex, theme, config
plat/       platform.h
overlay/    x11 · wayland · sdl · fbdev · console · win32 · winconsole · dos · wasm
docs/       BUILD.md
scripts/    smoke, release helpers, install-local
```

`core/` must stay C89 (`make ansi-check`); each overlay adds a platform layer.
See [`overlay/README.md`](overlay/README.md).

```sh
make release
sh publish-github.sh
```
