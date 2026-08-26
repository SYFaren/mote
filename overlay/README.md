# mote overlays

One **ANSI C89 core** (`../../core`) + one overlay at link time.

| Overlay | Target | Output | Build |
|---------|--------|--------|-------|
| `console/` | Linux / *BSD / macOS TTY | truecolor cells → stdout | `make console` |
| `x11/` | Linux GUI | X11 window | `make` / `make x11` |
| `wayland/` | Linux GUI | Wayland xdg-shell + wl_shm | `make wayland` |
| `sdl/` | Cross GUI | SDL2 (or SDL3 with `MOTE_SDL3=1`) | `make sdl` / `make sdl3` |
| `fbdev/` | Linux KMS-less | `/dev/fb0` software FB | `make fbdev` |
| `win32/` | Windows GUI | GDI window | `make win32` |
| `winconsole/` | Windows console | ConHost cells | `make winconsole` |
| `dos/` | FreeDOS / DOSBox | VGA text (DJGPP) | `make dos` |
| `wasm/` | Browser | Emscripten + SDL2 | `make wasm` (needs emsdk) |
| `soft/` | shared | RGB FB + bitmap font + GUI `main` | (linked in) |
| `posix/os.c` | Unix FS | fopen / config path | console, x11, wayland, sdl, fbdev, wasm |
| `win32/os.c` | Win FS | UTF-16 / `%APPDATA%` | win32, winconsole |

Shared squeeze flags: [`build.mk`](build.mk).

## Release layout (`make release`)

```text
dist-release/
  by-platform/
    linux/{x11,wayland,sdl2,sdl3?,console,fbdev}/
    windows/{gui,console}/
    dos/
    web/wasm/
  flat/                     # stable GitHub asset names
  mote-all-platforms.zip    # whole tree, categorized
  SHA256SUMS
```
