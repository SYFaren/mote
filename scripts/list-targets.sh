#!/bin/sh
# Print supported release targets (see docs/PLATFORMS.md).
set -eu

cat <<'EOF'
mote release targets — naming: mote-<os>-<arch>-<backend>

Tier 1 (primary release):
  linux   amd64  console x11 sdl2 wayland fbdev
  linux   arm64  console
  linux   armhf  console
  linux   i686   console
  freebsd amd64  console x11 sdl2
  freebsd arm64  console x11 sdl2
  windows amd64  gui winconsole
  dos     i686   dos
  web     —      wasm

Tier 2 (cross / secondary):
  linux   riscv64 console
  openbsd amd64   console x11
  netbsd  amd64   console x11
  windows i686    gui winconsole

Build:
  make release-linux          # native linux amd64 (all backends)
  make release-cross-linux    # linux arm/i686 + windows i686 from Debian
  make release-bsd            # on FreeBSD/OpenBSD/NetBSD
  sh scripts/build-port.sh <os> <arch> <backend>

Cross packages (Debian/Ubuntu):
  gcc-i686-linux-gnu gcc-aarch64-linux-gnu gcc-arm-linux-gnueabihf
  gcc-riscv64-linux-gnu gcc-mingw-w64-i686
EOF
