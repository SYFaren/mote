#!/bin/sh
# Native BSD release — run on FreeBSD, OpenBSD, or NetBSD.
set -eu

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"

case "$(uname -s 2>/dev/null)" in
  FreeBSD) OS=freebsd ;;
  OpenBSD) OS=openbsd ;;
  NetBSD)  OS=netbsd ;;
  *)
    echo "error: release-bsd requires a BSD host (got $(uname -s))" >&2
    exit 1
    ;;
esac

case "$(uname -m 2>/dev/null)" in
  amd64|x86_64) ARCH=amd64 ;;
  arm64|aarch64) ARCH=arm64 ;;
  *)
    echo "error: unsupported BSD arch $(uname -m)" >&2
    exit 1
    ;;
esac

echo "=== release-bsd: $OS $ARCH ==="

console_ok=0
for backend in console x11 sdl; do
  if sh "$ROOT/scripts/build-port.sh" "$OS" "$ARCH" "$backend"; then
    [ "$backend" = console ] && console_ok=1
  else
    echo "(skip $backend on $OS)"
  fi
done

[ "$console_ok" -eq 1 ] || exit 1
echo "=== release-bsd done ==="
