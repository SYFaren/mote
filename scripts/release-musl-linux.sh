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
    if [ "${MUSL_CROSS_BACKEND:-auto}" = auto ]; then
      if [ -n "${GITHUB_ACTIONS:-}" ]; then
        MUSL_CROSS_BACKEND=bootlin
      else
        MUSL_CROSS_BACKEND=musl.cc
      fi
      export MUSL_CROSS_BACKEND
    fi
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

for arch in arm64 armhf i686; do
  build_musl "$arch" console || echo "(skip musl $arch console)"
done
# riscv64 musl: no stable Bootlin toolchain; glibc cross build still ships in release-cross-linux.

echo "=== release-musl-linux done ==="
