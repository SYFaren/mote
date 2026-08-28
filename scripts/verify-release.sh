#!/bin/sh
# Smoke-test dist-release/flat binaries (native, qemu-user, wine).
set -eu

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
. "$ROOT/scripts/targets.sh"

FLAT="$ROOT/dist-release/flat"
fail=0

ok() { printf '  OK  %s\n' "$1"; }
bad() { printf ' FAIL %s\n' "$1"; fail=$((fail + 1)); }

test_linux() {
  bin="$1"
  base="$(basename "$bin")"
  # mote-linux-ARCH-backend or legacy mote-linux-console
  arch=amd64
  case "$base" in
    mote-linux-i686-*)    arch=i686 ;;
    mote-linux-arm64-*)   arch=arm64 ;;
    mote-linux-armhf-*)   arch=armhf ;;
    mote-linux-riscv64-*) arch=riscv64 ;;
    mote-linux-*)         arch=amd64 ;;
  esac

  if [ ! -f "$bin" ]; then
    bad "$base missing"
    return
  fi
  if ! file "$bin" | grep -q 'ELF.*executable'; then
    bad "$base not ELF"
    return
  fi

  runner="$bin"
  if [ "$arch" != amd64 ]; then
    qemu="$(target_qemu "$arch")"
    if [ -z "$qemu" ] || ! command -v "$qemu" >/dev/null 2>&1; then
      bad "$base (no qemu for $arch)"
      return
    fi
    case "$arch" in
      riscv64) export QEMU_LD_PREFIX="${QEMU_LD_PREFIX:-/usr/riscv64-linux-gnu}" ;;
    esac
    runner="$qemu $bin"
  fi

  if sh -c "$runner --version" >/dev/null 2>&1; then
    ok "$base --version"
  else
    bad "$base --version"
  fi
}

test_win() {
  bin="$1"
  base="$(basename "$bin")"
  [ -f "$bin" ] || { bad "$base missing"; return; }
  if ! command -v wine >/dev/null 2>&1; then
    ok "$base (wine skipped)"
    return
  fi
  if wine "$bin" --version >/dev/null 2>&1; then
    ok "$base wine --version"
  else
    bad "$base wine --version"
  fi
}

echo "== dist-release verify =="

if [ ! -d "$FLAT" ]; then
  echo "error: $FLAT missing (run make release)" >&2
  exit 1
fi

for f in "$FLAT"/mote-linux-*; do
  [ -f "$f" ] || continue
  case "$f" in *.upx) continue ;; esac
  test_linux "$f"
done

for f in "$FLAT"/mote-windows-*.exe; do
  [ -f "$f" ] || continue
  case "$f" in *.upx.exe) continue ;; esac
  test_win "$f"
done

if [ -f "$FLAT/mote-dos.exe" ]; then
  ok "mote-dos.exe present ($(file -b "$FLAT/mote-dos.exe"))"
fi

if [ -f "$FLAT/mote-web.zip" ]; then
  ok "mote-web.zip present"
fi

if [ "$fail" -ne 0 ]; then
  echo "verify FAILED ($fail)"
  exit 1
fi
echo "verify OK"
