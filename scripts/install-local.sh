#!/bin/sh
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
BIN="${HOME}/.local/bin"
mkdir -p "$BIN"

make test
make -C overlay/console
cp -f overlay/console/build/mote "$BIN/mote"
for port in "$@"; do
  make -C "overlay/$port"
  cp -f "overlay/$port/build/mote" "$BIN/mote-$port"
done

"$BIN/mote" --version
