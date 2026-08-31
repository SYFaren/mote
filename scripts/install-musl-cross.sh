#!/bin/sh
# Fetch musl.cc cross toolchains into ~/.local/opt/musl-cross (or MUSL_CROSS_ROOT).
set -eu

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
# shellcheck source=targets.sh
. "$ROOT/scripts/targets.sh"

MUSL_CROSS_ROOT="${MUSL_CROSS_ROOT:-$HOME/.local/opt/musl-cross}"
ARCH="${1:?arch (arm64 armhf i686 riscv64)}"

case "$ARCH" in
  arm64|armhf|i686|riscv64) ;;
  *)
    echo "error: install-musl-cross: unsupported arch $ARCH" >&2
    exit 1
    ;;
esac

DIRNAME="$(target_musl_cross_dir "$ARCH")"
PREFIX="$(target_musl_cross "$ARCH")"
TARBALL="${DIRNAME}.tgz"
URL="https://musl.cc/${TARBALL}"
DEST="$MUSL_CROSS_ROOT/$DIRNAME"
GCC="$DEST/bin/${PREFIX}gcc"

if [ -x "$GCC" ]; then
  echo "musl-cross: $ARCH ready ($GCC)"
  exit 0
fi

mkdir -p "$MUSL_CROSS_ROOT"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT INT HUP TERM

echo "musl-cross: downloading $URL"
if command -v curl >/dev/null 2>&1; then
  curl -fsSL "$URL" -o "$TMP/$TARBALL"
elif command -v wget >/dev/null 2>&1; then
  wget -q "$URL" -O "$TMP/$TARBALL"
else
  echo "error: need curl or wget" >&2
  exit 1
fi

echo "musl-cross: extracting $TARBALL"
tar -xzf "$TMP/$TARBALL" -C "$MUSL_CROSS_ROOT"
[ -x "$GCC" ] || {
  echo "error: expected $GCC after extract" >&2
  exit 1
}
echo "musl-cross: installed $GCC"
