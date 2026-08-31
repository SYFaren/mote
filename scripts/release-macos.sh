#!/bin/sh
# Native macOS release (console + SDL2).
set -eu

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"

[ "$(uname -s 2>/dev/null)" = "Darwin" ] || {
  echo "error: release-macos requires macOS (got $(uname -s))" >&2
  exit 1
}

case "$(uname -m 2>/dev/null)" in
  arm64) ARCH=arm64 ;;
  x86_64) ARCH=amd64 ;;
  *)
    echo "error: unsupported macOS arch $(uname -m)" >&2
    exit 1
    ;;
esac

echo "=== release-macos: $ARCH ==="

for backend in console sdl; do
  sh "$ROOT/scripts/build-port.sh" macos "$ARCH" "$backend"
done

echo "=== release-macos done ==="
