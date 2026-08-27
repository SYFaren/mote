#!/bin/sh
# Capture clean per-platform screenshots → mote-site/gallery/plat-*.png
# Full editor chrome (status bar included), no tiny postage stamps.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
SITE="${SITE_DIR:-$HOME/Projects/mote-site}"
GAL="$SITE/gallery"
DEMO="$ROOT/examples/hello_mote.c"
FRAME="$ROOT/scripts/shot_frame.py"
TMP="${TMPDIR:-/tmp}/mote-shots-$$"
mkdir -p "$GAL" "$TMP"
cd "$ROOT"
export PATH="${HOME}/.local/opt/djgpp/bin:${PATH:-}"
export SDL_VIDEODRIVER=x11

need() { command -v "$1" >/dev/null 2>&1; }

frame() {
  # $1 src $2 dst [ --nearest ]
  python3 "$FRAME" "$@"
}

# Soft framebuffer dump (pixel-perfect, always includes status).
shot_fb() {
  # $1 out name, $2 binary, rest env/args handled by caller via dump path
  out="$GAL/$1"
  ppm="$2"
  if [ -f "$ppm" ]; then
    convert "$ppm" "$TMP/fb.png"
    frame "$TMP/fb.png" "$out"
  else
    echo "  skip $1 (no fb dump)"
  fi
}

# Capture focused window by name/class via import (full client area).
shot_win() {
  outname="$1"
  raw="$TMP/$outname.raw.png"
  shift
  # remaining: shell snippet that starts the app and prints WID=...
  rm -f "$raw"
  if ! need xvfb-run || ! need import; then
    echo "  skip $outname (need xvfb-run+import)"
    return 0
  fi
  xvfb-run -a -s "-screen 0 1280x900x24" sh -c "
    set -e
    $*
    wid=\${WID:-}
    if [ -z \"\$wid\" ]; then
      echo 'no window' >&2
      exit 0
    fi
    # Give the first paint time (status bar).
    sleep 0.8
    import -window \"\$wid\" '$raw' || scrot -u -z '$raw' || scrot -z '$raw'
    kill \$APP_PID 2>/dev/null || true
    wait \$APP_PID 2>/dev/null || true
    [ -n \"\${EXTRA_KILL:-}\" ] && eval \"\$EXTRA_KILL\" || true
  " || true
  if [ -f "$raw" ]; then
    frame "$raw" "$GAL/$outname"
  else
    echo "  skip $outname (no capture)"
  fi
}

echo "== build =="
make -C overlay/x11 >/dev/null
make -C overlay/sdl >/dev/null
make -C overlay/console >/dev/null
make -C overlay/wayland >/dev/null
make -C overlay/win32 >/dev/null
make -C overlay/winconsole >/dev/null
make -C overlay/dos >/dev/null 2>/dev/null || true
# wasm optional
[ -f overlay/wasm/build/mote.html ] || make -C overlay/wasm >/dev/null 2>/dev/null || true

echo "== screenshots → $GAL =="

# --- SDL (soft FB) ---
rm -f "$TMP/sdl.ppm"
xvfb-run -a -s "-screen 0 1100x800x24" sh -c "
  MOTE_DUMP_FB='$TMP/sdl.ppm' ./overlay/sdl/build/mote -g 880x520 '$DEMO' >/dev/null 2>&1 &
  pid=\$!; sleep 1.6; kill \$pid 2>/dev/null; wait \$pid 2>/dev/null || true
" || true
shot_fb plat-linux-sdl2.png "$TMP/sdl.ppm"

# --- Wayland (soft FB via nested weston) ---
rm -f "$TMP/wayland.ppm"
if need weston; then
  xvfb-run -a -s "-screen 0 1200x900x24" sh -c "
    weston --backend=x11-backend.so --width=1000 --height=700 --socket=mote-wl-shot \
      >/tmp/weston-shot.log 2>&1 &
    echo \$! > '$TMP/weston.pid'
    sleep 1.8
    WAYLAND_DISPLAY=mote-wl-shot MOTE_DUMP_FB='$TMP/wayland.ppm' \
      ./overlay/wayland/build/mote -g 880x520 '$DEMO' >/tmp/wl-mote.log 2>&1 &
    echo \$! > '$TMP/mote.pid'
    sleep 2.2
    kill \$(cat '$TMP/mote.pid') \$(cat '$TMP/weston.pid') 2>/dev/null || true
  " || true
  shot_fb plat-linux-wayland.png "$TMP/wayland.ppm"
else
  echo "  skip plat-linux-wayland.png (no weston)"
fi

# --- X11 ---
shot_win plat-linux-x11.png '
  ./overlay/x11/build/mote -g 880x520 "'"$DEMO"'" >/dev/null 2>&1 &
  APP_PID=$!
  for i in 1 2 3 4 5 6 7 8 9 10; do
    WID=$(xdotool search --pid $APP_PID 2>/dev/null | tail -1)
    [ -n "$WID" ] && break
    WID=$(xdotool search --name mote 2>/dev/null | tail -1)
    [ -n "$WID" ] && break
    sleep 0.25
  done
  export WID APP_PID
'

# --- Console (xterm) — capture full xterm window so status row is kept ---
shot_win plat-linux-console.png '
  xterm -geometry 110x36 -fa "DejaVu Sans Mono" -fs 12 \
    -bg "#1e1e1e" -fg "#d4d4d4" -T mote-con-shot \
    -e ./overlay/console/build/mote "'"$DEMO"'" &
  APP_PID=$!
  for i in 1 2 3 4 5 6 7 8 9 10 11 12; do
    WID=$(xdotool search --name mote-con-shot 2>/dev/null | tail -1)
    [ -n "$WID" ] && break
    WID=$(xdotool search --class xterm 2>/dev/null | tail -1)
    [ -n "$WID" ] && break
    sleep 0.25
  done
  export WID APP_PID
'

# --- Windows GUI (Wine) ---
if need wine && [ -x overlay/win32/build/mote.exe ]; then
  shot_win plat-windows-gui.png '
    wine ./overlay/win32/build/mote.exe -g 880x520 "'"$DEMO"'" >/dev/null 2>&1 &
    APP_PID=$!
    sleep 2.0
    for i in 1 2 3 4 5 6 7 8; do
      WID=$(xdotool search --pid $APP_PID 2>/dev/null | tail -1)
      [ -n "$WID" ] && break
      WID=$(xdotool search --name mote 2>/dev/null | tail -1)
      [ -n "$WID" ] && break
      sleep 0.3
    done
    EXTRA_KILL="wineserver -k 2>/dev/null || true"
    export WID APP_PID EXTRA_KILL
  '
fi

# --- Windows console ---
# wineconsole --backend=user is flaky across Wine builds; prefer a direct
# wineconsole host, then reject ANSI-garbage / empty frames.
if need wine && [ -x overlay/winconsole/build/mote.exe ]; then
  raw="$TMP/winconsole.png"
  rm -f "$raw"
  xvfb-run -a -s "-screen 0 1400x1000x24" env HOME="${HOME:-/home/syfaren}" WINEDEBUG=-all sh -c "
    wineconsole ./overlay/winconsole/build/mote.exe '$DEMO' &
    pid=\$!
    sleep 6
    wid=\$(xdotool search --name mote 2>/dev/null | tail -1)
    [ -z \"\$wid\" ] && wid=\$(xdotool search --pid \$pid 2>/dev/null | tail -1)
    if [ -n \"\$wid\" ]; then
      import -window \"\$wid\" '$raw' 2>/dev/null || true
    fi
    [ -f '$raw' ] || scrot -z '$raw' 2>/dev/null || true
    kill \$pid 2>/dev/null || true
    wineserver -k 2>/dev/null || true
  " || true
  if [ -f "$raw" ] && python3 - "$raw" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
px, w, h = im.load(), im.size[0], im.size[1]
# Reject raw ANSI dumps (lots of '[' glyphs / low color) and blank frames.
color = sum(
    1
    for y in range(0, h, 2)
    for x in range(0, w, 2)
    if max(px[x, y]) - min(px[x, y]) > 35
)
lit = sum(1 for y in range(0, h, 2) for x in range(0, w, 2) if sum(px[x, y]) > 40)
raise SystemExit(0 if color > 800 and lit > 4000 else 1)
PY
  then
    frame "$raw" "$GAL/plat-windows-console.png"
  else
    echo "  skip plat-windows-console.png (Wine console capture unsuitable)"
  fi
fi

# --- DOS ---
if need dosbox && [ -x overlay/dos/build/mote.exe ]; then
  conf="$TMP/dos.conf"
  mkdir -p "$TMP/dos"
  cp -f overlay/dos/build/mote.exe "$TMP/dos/MOTE.EXE"
  cp -f "$DEMO" "$TMP/dos/HELLO.C"
  [ -f overlay/dos/runtime/CWSDPMI.EXE ] && cp -f overlay/dos/runtime/CWSDPMI.EXE "$TMP/dos/"
  cat > "$conf" <<EOF
[sdl]
fullscreen=false
output=surface
autolock=false
windowresolution=800x600
[cpu]
cycles=25000
[autoexec]
mount c $TMP/dos
c:
MOTE.EXE HELLO.C
EOF
  raw="$TMP/dos.raw.png"
  rm -f "$raw"
  xvfb-run -a -s "-screen 0 1024x768x24" sh -c "
    dosbox -conf '$conf' >/tmp/dos-shot.log 2>&1 &
    pid=\$!
    sleep 5.0
    wid=\$(xdotool search --name DOSBox 2>/dev/null | head -1)
    if [ -n \"\$wid\" ]; then
      import -window \"\$wid\" '$raw' 2>/dev/null || true
    fi
    [ -f '$raw' ] || scrot -z '$raw' 2>/dev/null || true
    kill \$pid 2>/dev/null || true
    wait \$pid 2>/dev/null || true
  " || true
  if [ -f "$raw" ]; then
    frame "$raw" "$GAL/plat-dos.png" --nearest
  else
    echo "  skip plat-dos.png"
  fi
fi

# --- WASM (headless Chrome — reliable full paint) ---
if [ -f overlay/wasm/build/mote.html ] && need google-chrome; then
  raw="$TMP/wasm.raw.png"
  rm -f "$raw"
  (
    cd overlay/wasm/build
    python3 -m http.server 8766 >/tmp/mote-wasm-http.log 2>&1 &
    echo $! > "$TMP/http.pid"
  )
  sleep 0.8
  google-chrome --headless=new --disable-gpu --hide-scrollbars \
    --window-size=1100,720 \
    --screenshot="$raw" \
    "http://127.0.0.1:8766/mote.html" >/tmp/chrome-wasm.log 2>&1 || true
  kill "$(cat "$TMP/http.pid")" 2>/dev/null || true
  if [ -f "$raw" ]; then
    # Reject blank black frames
    if python3 - "$raw" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
px, w, h = im.load(), im.size[0], im.size[1]
lit = sum(1 for y in range(0, h, 3) for x in range(0, w, 3) if sum(px[x, y]) > 40)
raise SystemExit(0 if lit > 2000 else 1)
PY
    then
      frame "$raw" "$GAL/plat-web-wasm.png"
    else
      echo "  skip plat-web-wasm.png (blank)"
    fi
  else
    echo "  skip plat-web-wasm.png"
  fi
fi

rm -rf "$TMP"
echo "done:"
ls -la "$GAL"/plat-*.png 2>/dev/null || true
