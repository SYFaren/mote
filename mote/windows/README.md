# mote (Windows)

Independent Windows codebase. Developer: **SYFaren**

## Layout

```
src/
  main.c
  common/     shared constants
  core/       gap buffer, utf8, undo
  editor/     editing UI logic
  platform/   Win32 backend (win32.c)
```

## Build (MinGW cross from Linux)

```sh
cd mote/windows
make
make pack          # UPX → build/mote.packed.exe
```

Needs: `mingw-w64`. UPX optional (`~/.local/opt/upx`).

`mote.exe` is fully static (no MinGW DLLs). Packed copy is smaller on disk;
runtime decompresses in memory.
