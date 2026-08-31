#!/bin/sh
# Static musl Linux ports (console + fbdev). GUI needs musl-linked X11/SDL — not here yet.
set -eu

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
# shellcheck source=targets.sh
. "$ROOT/scripts/targets.sh"

MUSL_CROSS_ROOT="${MUSL_CROSS_ROOT:-$HOME/.local/opt/musl-cross}"

build_musl() {
  arch="$1"
  backend="$2"
  if [ "$arch" != amd64 ]; then
    sh "$ROOT/scripts/install-musl-cross.sh" "$arch"
    dir="$(target_musl_cross_dir "$arch")"
    PATH="$MUSL_CROSS_ROOT/$dir/bin:$PATH" MOTE_LIBC=musl sh "$ROOT/scripts/build-port.sh" linux "$arch" "$backend"
  else
    MOTE_LIBC=musl sh "$ROOT/scripts/build-port.sh" linux "$arch" "$backend"
  fi
}

echo "=== release-musl-linux ==="

if command -v musl-gcc >/dev/null 2>&1; then
  build_musl amd64 console
  build_musl amd64 fbdev || true
else
  echo "(skip linux-amd64-musl — no musl-gcc; install musl-tools)"
fi

for arch in arm64 armhf i686 riscv64; do
  build_musl "$arch" console || echo "(skip musl $arch console)"
done

echo "=== release-musl-linux done ==="
