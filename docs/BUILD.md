# Build

## Overlays

| Target | Command | Deps |
|--------|---------|------|
| Linux X11 | `make x11` | `libx11-dev` |
| Linux Wayland | `make wayland` | `libwayland-dev`, `libxkbcommon-dev`, `wayland-protocols` |
| Linux SDL2 | `make sdl` | `libsdl2-dev` |
| Linux SDL3 | `make sdl3` | `pkg-config sdl3` |
| Linux fbdev | `make fbdev` | `/dev/fb0` |
| Unix console | `make console` | TTY |
| Windows GUI | `make win32` | MinGW |
| Windows console | `make winconsole` | MinGW |
| DOS | `make dos` | DJGPP on `PATH` |
| WebAssembly | `make wasm` | emscripten (`~/.local/opt/emsdk`) |

Quality gate: `make ansi-check && make test && make smoke`

## Release

```sh
make release              # everything this host can build
make release-linux        # linux amd64 backends
make release-cross-linux  # linux arm/i686/riscv64 + windows i686
make release-bsd          # on FreeBSD / OpenBSD / NetBSD only
make verify-release       # smoke-test dist-release/flat
```

Output:

```text
dist-release/
  by-platform/<os>/<arch>/<backend>/mote[.upx]
  flat/mote-<os>-<arch>-<backend>   # GitHub asset names
  flat/mote-linux-console …         # legacy linux amd64 aliases
  mote-all-platforms.zip
  SHA256SUMS
```

Single port: `sh scripts/build-port.sh <os> <arch> <backend>`

| OS | arch | backends in release |
|----|------|---------------------|
| linux | amd64 | console, x11, sdl2, wayland, fbdev |
| linux | arm64, armhf, i686 | console, x11, sdl2 |
| linux | riscv64 | console |
| windows | amd64, i686 | gui, winconsole |
| dos | i686 | dos |
| web | — | wasm |

Cross on Debian: `gcc-aarch64-linux-gnu`, `gcc-arm-linux-gnueabihf`, `gcc-i686-linux-gnu`, `gcc-riscv64-linux-gnu`, `gcc-mingw-w64-i686`.

Publish: `make release && sh publish-github.sh` (notes in `docs/releases/<tag>.md`).

UPX: set `UPX_BIN` (default `~/.local/opt/upx/upx`).
