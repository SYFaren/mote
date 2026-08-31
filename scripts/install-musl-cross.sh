#!/bin/sh
# Fetch musl cross toolchains into ~/.local/opt/musl-cross (or MUSL_CROSS_ROOT).
# CI: Bootlin (toolchains.bootlin.com). Local: musl.cc with Bootlin fallback.
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

resolve_musl_cross_backend() {
  case "${MUSL_CROSS_BACKEND:-auto}" in
    bootlin|musl.cc) printf '%s\n' "${MUSL_CROSS_BACKEND}" ;;
    auto)
      if [ -n "${GITHUB_ACTIONS:-}" ]; then
        printf 'bootlin\n'
      else
        printf 'musl.cc\n'
      fi
      ;;
    *)
      echo "error: unknown MUSL_CROSS_BACKEND=${MUSL_CROSS_BACKEND}" >&2
      return 1
      ;;
  esac
}

BACKEND="$(resolve_musl_cross_backend)"
export MUSL_CROSS_BACKEND="$BACKEND"

PREFIX="$(target_musl_cross "$ARCH")"
DIRNAME="$(target_musl_cross_dir "$ARCH")"
DEST="$MUSL_CROSS_ROOT/$DIRNAME"
GCC="$DEST/bin/${PREFIX}gcc"

if [ -x "$GCC" ]; then
  echo "musl-cross: $ARCH ready ($GCC)"
  exit 0
fi

mkdir -p "$MUSL_CROSS_ROOT"
TMP="$(mktemp -d)"
trap 'rm -rf "$TMP"' EXIT INT HUP TERM

install_bootlin() {
  ver="${BOOTLIN_VERSION:-2024.05-1}"
  case "$ARCH" in
    arm64)   name="aarch64--musl--stable-${ver}"; path="aarch64" ;;
    armhf)   name="armv7-eabihf--musl--stable-${ver}"; path="armv7-eabihf" ;;
    i686)    name="x86-i686--musl--stable-${ver}"; path="x86-i686" ;;
    riscv64) name="riscv64--musl--stable-${ver}"; path="riscv64" ;;
  esac
  url="https://toolchains.bootlin.com/downloads/releases/toolchains/${path}/tarballs/${name}.tar.xz"
  echo "musl-cross: downloading $url"
  curl -fsSL --retry 5 --retry-all-errors --retry-delay 10 \
    --connect-timeout 30 --max-time 900 "$url" -o "$TMP/toolchain.tar.xz"
  echo "musl-cross: extracting $name"
  tar -xJf "$TMP/toolchain.tar.xz" -C "$MUSL_CROSS_ROOT"
  [ -x "$MUSL_CROSS_ROOT/$name/bin/${PREFIX}gcc" ] || {
    echo "error: expected $MUSL_CROSS_ROOT/$name/bin/${PREFIX}gcc" >&2
    return 1
  }
  echo "musl-cross: installed $MUSL_CROSS_ROOT/$name/bin/${PREFIX}gcc"
}

install_musl_cc() {
  tarball="${DIRNAME}.tgz"
  url="https://musl.cc/${tarball}"
  echo "musl-cross: downloading $url"
  if ! curl -fsSL --retry 3 --retry-all-errors --retry-delay 10 \
    --connect-timeout 30 --max-time 300 "$url" -o "$TMP/$tarball"; then
    echo "musl-cross: musl.cc unreachable, trying Bootlin" >&2
    MUSL_CROSS_BACKEND=bootlin
    export MUSL_CROSS_BACKEND
    PREFIX="$(target_musl_cross "$ARCH")"
    DIRNAME="$(target_musl_cross_dir "$ARCH")"
    DEST="$MUSL_CROSS_ROOT/$DIRNAME"
    GCC="$DEST/bin/${PREFIX}gcc"
    install_bootlin
    return 0
  fi
  echo "musl-cross: extracting $tarball"
  tar -xzf "$TMP/$tarball" -C "$MUSL_CROSS_ROOT"
  [ -x "$GCC" ] || {
    echo "error: expected $GCC after extract" >&2
    return 1
  }
  echo "musl-cross: installed $GCC"
}

case "$BACKEND" in
  bootlin) install_bootlin ;;
  musl.cc) install_musl_cc ;;
esac
