# mote

Tiny multi-platform text editor — **ANSI C89 core** + compile-time **overlays**.

```text
core/          editor, buffer, utf8, undo, hl, theme, config
plat/          platform.h contract
overlay/       x11 · wayland · sdl · fbdev · console · win32 · winconsole · dos · wasm
docs/          build notes
scripts/       smoke.sh · shots.sh
```

## Quick start

```sh
make                 # Linux X11
make console         # Unix TTY
make sdl             # SDL2
make wayland         # Wayland
make win32           # Windows GUI (MinGW)
make wasm            # browser (needs emsdk on PATH)
make test && make smoke
```

## Releases

```sh
make release
# → dist-release/by-platform/…   (folders by OS)
# → dist-release/flat/…          (stable asset names)
# → dist-release/mote-all-platforms.zip
```

See [`docs/BUILD.md`](docs/BUILD.md) and [`overlay/README.md`](overlay/README.md).
