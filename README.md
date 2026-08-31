# mote

Tiny multi-platform text editor — **ANSI C89 core** + compile-time **overlays**.

```text
core/       buffer, editor, utf8, undo, hl, theme, config
plat/       platform.h
overlay/    x11 · wayland · sdl · fbdev · console · win32 · winconsole · dos · wasm
docs/       BUILD.md
scripts/    smoke, release helpers, install-local
```

## Quick start

```sh
make                 # Linux X11
make console         # Unix TTY
make test && make smoke
```

## Release

```sh
make release
sh publish-github.sh
```

See [`docs/BUILD.md`](docs/BUILD.md) and [`overlay/README.md`](overlay/README.md).
