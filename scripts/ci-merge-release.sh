#!/bin/sh
# Merge dist-release/ trees from CI platform jobs and rebuild zip + checksums.
set -eu

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
DIST="$ROOT/dist-release"
CAT="$DIST/by-platform"
FLAT="$DIST/flat"

mkdir -p "$CAT" "$FLAT"

for src in "$@"; do
  [ -d "$src" ] || continue
  if [ -d "$src/by-platform" ]; then
    (cd "$src/by-platform" && tar cf - .) | (cd "$CAT" && tar xf -)
  fi
  if [ -d "$src/flat" ]; then
    for f in "$src/flat/"*; do
      [ -f "$f" ] || continue
      cp -f "$f" "$FLAT/"
    done
  fi
done

if [ -d "$FLAT" ] && ls "$FLAT/"* >/dev/null 2>&1; then
  (cd "$FLAT" && sha256sum * > "$DIST/SHA256SUMS")
  cp -f "$DIST/SHA256SUMS" "$FLAT/SHA256SUMS"
fi

printf '%s\n' \
  'mote multi-platform release' \
  '' \
  'by-platform/<os>/<arch>/<backend>/mote[.upx]' \
  'flat/ — GitHub asset names' \
  > "$CAT/README.txt"

rm -f "$DIST/mote-all-platforms.zip"
(cd "$DIST" && zip -r -q mote-all-platforms.zip by-platform flat SHA256SUMS)

# UPX for BSD flat names (also fills by-platform/*.upx when present)
if [ -x "$ROOT/scripts/pack-bsd-upx.sh" ]; then
  sh "$ROOT/scripts/pack-bsd-upx.sh" || true
  for f in "$FLAT"/mote-freebsd-*.upx "$FLAT"/mote-openbsd-*.upx; do
    [ -f "$f" ] || continue
    base="$(basename "$f" .upx)"
    os="${base#mote-}"; os="${os%%-*}"
    rest="${base#mote-$os-}"
    arch="${rest%%-*}"
    be="${rest#*-}"
    [ "$be" = sdl ] && be=sdl2
    dst="$CAT/$os/$arch/$be/mote.upx"
    [ -d "$CAT/$os/$arch/$be" ] || continue
    cp -f "$f" "$dst" 2>/dev/null || true
  done
  rm -f "$DIST/mote-all-platforms.zip"
  (cd "$FLAT" && sha256sum * > "$DIST/SHA256SUMS" 2>/dev/null || true)
  cp -f "$DIST/SHA256SUMS" "$FLAT/SHA256SUMS" 2>/dev/null || true
  (cd "$DIST" && zip -r -q mote-all-platforms.zip by-platform flat SHA256SUMS)
fi

echo "=== merged dist-release ==="
find "$DIST" -type f | sort
