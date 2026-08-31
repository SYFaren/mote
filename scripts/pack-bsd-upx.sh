#!/bin/sh
# Pack FreeBSD/OpenBSD flat ELFs with UPX (host tool — Linux CI can pack BSD ELF).
set -eu

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
FLAT="$ROOT/dist-release/flat"
UPX_BIN="${UPX_BIN:-upx}"

command -v "$UPX_BIN" >/dev/null 2>&1 || {
  echo "pack-bsd-upx: $UPX_BIN not found, skip" >&2
  exit 0
}

for f in "$FLAT"/mote-freebsd-* "$FLAT"/mote-openbsd-*; do
  [ -f "$f" ] || continue
  case "$f" in *.upx) continue ;; esac
  out="$f.upx"
  [ -f "$out" ] && continue
  tmp="$f.packtmp"
  cp -f "$f" "$tmp"
  if env -u UPX "$UPX_BIN" --best --lzma -f "$tmp" 2>&1; then
    mv -f "$tmp" "$out"
    echo "upx: $(basename "$out")"
  else
    echo "pack-bsd-upx: failed $(basename "$f")" >&2
    rm -f "$tmp"
  fi
done
