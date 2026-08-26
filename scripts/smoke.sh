#!/bin/sh
# Full platform smoke: build, --version, link, optional headless open.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
fail=0

ok() { printf '  OK  %s\n' "$1"; }
bad() { printf ' FAIL %s\n' "$1"; fail=$((fail + 1)); }

run_ver() {
  name="$1"
  bin="$2"
  if [ ! -x "$bin" ] && [ ! -f "$bin" ]; then
    bad "$name missing"
    return
  fi
  if "$bin" --version >/dev/null 2>&1; then
    ok "$name --version ($("$bin" --version 2>/dev/null | head -1))"
  else
    bad "$name --version"
  fi
}

echo "== ansi + unit =="
make ansi-check >/dev/null
make test >/dev/null
ok "ansi-check + test_core"

echo "== build overlays =="
make -C overlay/console >/dev/null && ok "build console" || bad "build console"
make -C overlay/x11 >/dev/null && ok "build x11" || bad "build x11"
make -C overlay/sdl >/dev/null && ok "build sdl2" || bad "build sdl2"
make -C overlay/wayland >/dev/null && ok "build wayland" || bad "build wayland"
make -C overlay/fbdev >/dev/null && ok "build fbdev" || bad "build fbdev"
make -C overlay/win32 >/dev/null && ok "build win32" || bad "build win32"
make -C overlay/winconsole >/dev/null && ok "build winconsole" || bad "build winconsole"

run_ver console overlay/console/build/mote
run_ver x11 overlay/x11/build/mote
run_ver sdl2 overlay/sdl/build/mote
run_ver wayland overlay/wayland/build/mote
run_ver fbdev overlay/fbdev/build/mote

if command -v wine >/dev/null 2>&1; then
  wine overlay/win32/build/mote.exe --version >/dev/null 2>&1 && ok "win32 wine --version" || bad "win32 wine"
  wine overlay/winconsole/build/mote.exe --version >/dev/null 2>&1 && ok "winconsole wine --version" || bad "winconsole wine"
else
  ok "wine skipped"
fi

if command -v i586-pc-msdosdjgpp-gcc >/dev/null 2>&1; then
  make -C overlay/dos >/dev/null && ok "build dos" || bad "build dos"
else
  ok "dos toolchain skipped"
fi

if command -v emcc >/dev/null 2>&1; then
  make -C overlay/wasm >/dev/null
  [ -f overlay/wasm/build/mote.wasm ] && [ -f overlay/wasm/build/mote.html ] && \
    [ -f overlay/wasm/build/mote.data ] && ok "wasm artifacts" || bad "wasm artifacts"
else
  ok "emcc skipped"
fi

echo "== link / deps =="
if command -v ldd >/dev/null 2>&1; then
  for b in overlay/x11/build/mote overlay/sdl/build/mote overlay/wayland/build/mote \
           overlay/console/build/mote overlay/fbdev/build/mote; do
    if ldd "$b" >/dev/null 2>&1; then ok "ldd $(basename $(dirname $(dirname $b)))"
    else bad "ldd $b"; fi
  done
fi

echo "== headless open (Xvfb) =="
if command -v xvfb-run >/dev/null 2>&1; then
  # Open file briefly; process must start without crash.
  for pair in "x11:overlay/x11/build/mote" "sdl2:overlay/sdl/build/mote"; do
    name=${pair%%:*}
    bin=${pair#*:}
    if xvfb-run -a -s "-screen 0 800x600x24" timeout 2s "$bin" -g 640x400 examples/hello_mote.c \
         >/tmp/mote-smoke-"$name".log 2>&1; then
      ok "headless $name exit0"
    else
      ec=$?
      # timeout returns 124 — that means it ran, which is success for smoke
      if [ "$ec" -eq 124 ]; then ok "headless $name ran (timeout)"
      else bad "headless $name (ec=$ec)"; fi
    fi
  done
  # Soft-FB help dump must contain the help panel (catches SHM/pixel-width bugs).
  rm -f /tmp/mote-smoke-help.ppm
  xvfb-run -a -s "-screen 0 900x600x24" sh -c "
    MOTE_START_HELP=1 MOTE_DUMP_FB=/tmp/mote-smoke-help.ppm \
      timeout 2s ./overlay/sdl/build/mote -H -g 720x420 examples/hello_mote.c \
      >/dev/null 2>&1 || true
  " || true
  if [ -f /tmp/mote-smoke-help.ppm ] && python3 - <<'PY'
from PIL import Image
im = Image.open("/tmp/mote-smoke-help.ppm").convert("RGB")
px, w, h = im.load(), im.size[0], im.size[1]
panel = sum(
    1
    for y in range(0, min(h, 300), 2)
    for x in range(0, min(w, 700), 2)
    if 18 <= px[x, y][0] <= 55 and 22 <= px[x, y][1] <= 60 and 28 <= px[x, y][2] <= 70
)
orange = sum(
    1
    for y in range(0, h, 2)
    for x in range(0, w, 2)
    if px[x, y][0] > 200 and 100 < px[x, y][1] < 180 and px[x, y][2] < 100
)
raise SystemExit(0 if (panel > 2000 or orange > 200) else 1)
PY
  then
    ok "sdl help dump has panel"
  else
    bad "sdl help dump missing panel"
  fi
else
  ok "xvfb-run skipped"
fi

echo "== release layout =="
if [ -d dist-release/by-platform ] && [ -f dist-release/mote-all-platforms.zip ]; then
  ok "dist-release categorized + zip"
else
  ok "dist-release not built yet (run make release)"
fi

if [ "$fail" -ne 0 ]; then
  echo "smoke FAILED ($fail)"
  exit 1
fi
echo "smoke OK"
