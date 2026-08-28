#!/bin/sh
# Build and install fresh mote binaries for local use.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BIN="${HOME}/.local/bin"
export PATH="${HOME}/.local/opt/djgpp/bin:${HOME}/.local/opt/emsdk/upstream/emscripten:${HOME}/.local/opt/emsdk:${PATH}"
mkdir -p "$BIN"

echo "== build =="
make test
make -C overlay/console
make -C overlay/x11
make -C overlay/sdl
make -C overlay/wayland

echo "== install $BIN =="
cp -f overlay/console/build/mote "$BIN/mote"
cp -f overlay/x11/build/mote "$BIN/mote-x11"
cp -f overlay/sdl/build/mote "$BIN/mote-sdl"
cp -f overlay/wayland/build/mote "$BIN/mote-wayland"

echo "== dist-release =="
make release-linux

echo ""
echo "Installed:"
ls -la "$BIN/mote" "$BIN/mote-x11" 2>/dev/null || true
"$BIN/mote" --version
echo "Help check (console):"
strings "$BIN/mote" | grep 'F8/Alt+B set' || echo "MISSING new help string!"
