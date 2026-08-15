# mote-x

Extended **mote** by **SYFaren** — same family, more capability.

Soft size budget **~100–200 KB**. Still not an IDE.

| Folder | Role |
|--------|------|
| [linux/](linux/) | X11 |
| [windows/](windows/) | Win32 |

```sh
cd mote-x/linux && make && make test && make pack
cd mote-x/windows && make && make test && make pack
```

## What’s in mote-x (vs mote)

- Syntax highlighting for popular languages
- Themes: dark / light / slate (`Ctrl+T`)
- Soft wrap (`Ctrl+W`), visible whitespace (`F7`), font zoom (`Ctrl+=/-/0`)
- Multi-document (up to 6): `Ctrl+N`, `Ctrl+Tab` / `Ctrl+Shift+Tab`, close with `Ctrl+Shift+W` (Windows also `Ctrl+F4`)
- Find: next (`F3`), previous (`Shift+F3`); flags case (`Alt+C`), word (`Alt+W`); highlight all matches
- Jump to matching bracket (`Ctrl+]`)
- Indent/outdent, delete/duplicate line, autoclose brackets
- Dirty open: new tab when possible; at max docs asks save/discard before open
- EOL LF/CRLF, reload, readonly, recent files
- Line numbers, current-line highlight, bracket matching

Help: **F1** / **Ctrl+H**.

Sister folder: [../mote](../mote) — original ultra-compact editor.
