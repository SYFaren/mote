# Build & platforms

## Matrix

| Product | Host build | Notes |
|---------|------------|--------|
| Linux X11 | `make x11` | `libx11-dev` |
| Linux Wayland | `make wayland` | `libwayland-dev`, `libxkbcommon-dev`, `wayland-protocols` |
| Linux SDL2 | `make sdl` | `libsdl2-dev` |
| Linux SDL3 | `make sdl3` | needs `pkg-config sdl3` |
| Linux fbdev | `make fbdev` | `/dev/fb0` (often needs permissions) |
| Unix console | `make console` | real TTY, truecolor |
| Windows GUI | `make win32` | MinGW |
| Windows console | `make winconsole` | MinGW `-mconsole` |
| DOS | `make dos` | DJGPP on `PATH` |
| WebAssembly | `make wasm` | `source ~/.local/opt/emsdk/emsdk_env.sh` |

Core quality gate:

```sh
make ansi-check
make test
make smoke          # builds + --version across overlays
```

## Release bundle

```sh
make release          # categorized by-platform/ + flat/ + zip + SHA256SUMS
make release-linux
make release-windows
make release-dos
make release-extra    # wasm (+ sdl3 if available)
```

```text
dist-release/
  by-platform/…          # folders by OS / backend
  flat/mote-linux-x11 …  # stable names for direct links
  flat/mote-web.zip      # wasm bundle (html+js+wasm+data)
  mote-all-platforms.zip
  SHA256SUMS
```

Live wasm demo for the download site: copy `overlay/wasm/build/*` into
`mote-site/play/` (same four files the zip contains).

Screenshots (optional): `sh scripts/shots.sh` → writes into `mote-site/gallery/plat-*.png`.

Packed variants (`*.upx`) need the UPX binary. Makefiles use `UPX_BIN`
(default `~/.local/opt/upx/upx`) and run it with `env -u UPX` so a stray
`UPX=…` shell variable (UPX’s own options channel) cannot break packing.

Publish: `sh publish-github.sh` after `make release`
(optional: `TAG=v2.0.0 sh publish-github.sh`; default tag is `v2.0.0`).
