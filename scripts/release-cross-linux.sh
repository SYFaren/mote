#!/bin/sh
# Cross-build Linux (and Windows i686) release ports from a Linux host.
set -eu

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
. "$ROOT/scripts/targets.sh"

build_if_cc() {
  os="$1"
  arch="$2"
  backend="$3"
  cross="$(target_cross "$os" "$arch")" || return 0
  cc="${cross}gcc"
  command -v "$cc" >/dev/null 2>&1 || {
    echo "(skip $os-$arch-$backend — no $cc)"
    return 0
  }
  sh "$ROOT/scripts/build-port.sh" "$os" "$arch" "$backend"
}

echo "=== release-cross-linux ==="

for arch in i686 arm64 armhf riscv64; do
  build_if_cc linux "$arch" console
done

# Windows i686 (amd64 comes from release-windows)
for arch in i686; do
  build_if_cc windows "$arch" gui || true
  build_if_cc windows "$arch" winconsole || true
done

echo "=== release-cross-linux done ==="
