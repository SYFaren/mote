#!/bin/sh
# Capture F1/help overlays → demo-media/help/ (+ copy to mote-site/gallery)
# Full editor + help panel + status bar, no crooked/cropped frames.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
HELP="$ROOT/demo-media/help"
GAL="${SITE_DIR:-$HOME/Projects/mote-site}/gallery"
DEMO="$ROOT/examples/hello_mote.c"
FRAME="$ROOT/scripts/shot_frame.py"
TMP="${TMPDIR:-/tmp}/mote-help-shots-$$"
mkdir -p "$HELP" "$TMP"
cd "$ROOT"
export PATH="${HOME}/.local/opt/djgpp/bin:${PATH:-}"
export SDL_VIDEODRIVER=x11

need() { command -v "$1" >/dev/null 2>&1; }
frame() { python3 "$FRAME" "$@"; }

echo "== build =="
make -C overlay/x11 >/dev/null
make -C overlay/sdl >/dev/null
make -C overlay/console >/dev/null
make -C overlay/wayland >/dev/null
make -C overlay/win32 >/dev/null 2>/dev/null || true
make -C overlay/winconsole >/dev/null 2>/dev/null || true
make -C overlay/dos >/dev/null 2>/dev/null || true

echo "== help shots =="

# SDL soft-FB
rm -f "$TMP/sdl.ppm"
xvfb-run -a -s "-screen 0 1100x800x24" sh -c "
  MOTE_START_HELP=1 MOTE_DUMP_FB='$TMP/sdl.ppm' \
    ./overlay/sdl/build/mote -g 880x520 '$DEMO' >/dev/null 2>&1 &
  pid=\$!; sleep 1.6; kill \$pid 2>/dev/null; wait \$pid 2>/dev/null || true
"
if [ -f "$TMP/sdl.ppm" ]; then
  convert "$TMP/sdl.ppm" "$TMP/sdl.png"
  frame "$TMP/sdl.png" "$HELP/help-linux-sdl2.png"
fi

# Wayland soft-FB
rm -f "$TMP/wl.ppm"
if need weston; then
  xvfb-run -a -s "-screen 0 1200x900x24" sh -c "
    weston --backend=x11-backend.so --width=1000 --height=700 --socket=mote-help \
      >/tmp/weston-help.log 2>&1 &
    w=\$!; sleep 1.7
    WAYLAND_DISPLAY=mote-help MOTE_START_HELP=1 MOTE_DUMP_FB='$TMP/wl.ppm' \
      ./overlay/wayland/build/mote -g 880x520 '$DEMO' >/dev/null 2>&1 &
    m=\$!; sleep 2.2; kill \$m \$w 2>/dev/null; wait 2>/dev/null || true
  " || true
  if [ -f "$TMP/wl.ppm" ]; then
    convert "$TMP/wl.ppm" "$TMP/wl.png"
    frame "$TMP/wl.png" "$HELP/help-linux-wayland.png"
  fi
fi

# X11
rm -f "$TMP/x11.png"
xvfb-run -a -s "-screen 0 1280x900x24" sh -c "
  MOTE_START_HELP=1 ./overlay/x11/build/mote -g 880x520 '$DEMO' >/dev/null 2>&1 &
  pid=\$!
  for i in 1 2 3 4 5 6 7 8 9 10; do
    wid=\$(xdotool search --pid \$pid 2>/dev/null | tail -1)
    [ -n \"\$wid\" ] && break
    sleep 0.25
  done
  sleep 0.9
  if [ -n \"\$wid\" ]; then
    import -window \"\$wid\" '$TMP/x11.png'
  else
    scrot -z '$TMP/x11.png'
  fi
  kill \$pid 2>/dev/null; wait \$pid 2>/dev/null || true
" || true
[ -f "$TMP/x11.png" ] && frame "$TMP/x11.png" "$HELP/help-linux-x11.png"

# Console
rm -f "$TMP/con.png"
xvfb-run -a -s "-screen 0 1280x900x24" sh -c "
  MOTE_START_HELP=1 xterm -geometry 110x36 -fa 'DejaVu Sans Mono' -fs 12 \
    -bg '#1e1e1e' -fg '#d4d4d4' -T mote-help-con \
    -e ./overlay/console/build/mote '$DEMO' &
  pid=\$!
  for i in 1 2 3 4 5 6 7 8 9 10 11 12; do
    wid=\$(xdotool search --name mote-help-con 2>/dev/null | tail -1)
    [ -n \"\$wid\" ] && break
    sleep 0.25
  done
  sleep 1.0
  if [ -n \"\$wid\" ]; then
    import -window \"\$wid\" '$TMP/con.png'
  else
    scrot -z '$TMP/con.png'
  fi
  kill \$pid 2>/dev/null; wait \$pid 2>/dev/null || true
" || true
[ -f "$TMP/con.png" ] && frame "$TMP/con.png" "$HELP/help-linux-console.png"

# Windows GUI
if [ -x overlay/win32/build/mote.exe ] && need wine; then
  rm -f "$TMP/w32.png"
  xvfb-run -a -s "-screen 0 1280x900x24" sh -c "
    MOTE_START_HELP=1 wine ./overlay/win32/build/mote.exe -g 880x520 '$DEMO' >/dev/null 2>&1 &
    pid=\$!
    sleep 2.4
    wid=\$(xdotool search --pid \$pid 2>/dev/null | tail -1)
    [ -z \"\$wid\" ] && wid=\$(xdotool search --name mote 2>/dev/null | tail -1)
    sleep 0.5
    if [ -n \"\$wid\" ]; then
      import -window \"\$wid\" '$TMP/w32.png'
    else
      scrot -z '$TMP/w32.png'
    fi
    kill \$pid 2>/dev/null; wait \$pid 2>/dev/null || true
    wineserver -k 2>/dev/null || true
  " || true
  [ -f "$TMP/w32.png" ] && frame "$TMP/w32.png" "$HELP/help-windows-gui.png"
fi

# Windows console
if [ -x overlay/winconsole/build/mote.exe ] && need wine; then
  rm -f "$TMP/wc.png"
  xvfb-run -a -s "-screen 0 1280x900x24" sh -c "
    MOTE_START_HELP=1 wineconsole --backend=user ./overlay/winconsole/build/mote.exe -g 100x32 '$DEMO' >/dev/null 2>&1 &
    pid=\$!
    sleep 4.5
    wid=\$(xdotool search --name wineconsole 2>/dev/null | head -1)
    [ -z \"\$wid\" ] && wid=\$(xdotool search --name mote 2>/dev/null | head -1)
    if [ -n \"\$wid\" ]; then
      import -window \"\$wid\" '$TMP/wc.png' 2>/dev/null || scrot -u -z '$TMP/wc.png'
    else
      scrot -z '$TMP/wc.png'
    fi
    kill \$pid 2>/dev/null; wait \$pid 2>/dev/null || true
    wineserver -k 2>/dev/null || true
  " || true
  if [ -f "$TMP/wc.png" ]; then
    if python3 - "$TMP/wc.png" <<'PY'
import sys
from PIL import Image
im = Image.open(sys.argv[1]).convert("RGB")
px, w, h = im.load(), im.size[0], im.size[1]
colorful = sum(
    1
    for y in range(0, h, 2)
    for x in range(0, w, 2)
    if max(px[x, y]) - min(px[x, y]) > 40 and sum(px[x, y]) > 80
)
raise SystemExit(0 if colorful > 400 else 1)
PY
    then
      frame "$TMP/wc.png" "$HELP/help-windows-console.png"
    else
      echo "  help-windows-console.png skipped (blank under Wine)"
    fi
  fi
fi

# DOS help
if [ -x overlay/dos/build/mote.exe ] && need dosbox; then
  conf="$TMP/dos.conf"
  mkdir -p "$TMP/dos"
  cp -f overlay/dos/build/mote.exe "$TMP/dos/MOTE.EXE"
  [ -f overlay/dos/runtime/CWSDPMI.EXE ] && cp -f overlay/dos/runtime/CWSDPMI.EXE "$TMP/dos/"
  cp -f "$DEMO" "$TMP/dos/HELLO.C"
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
MOTE.EXE -H HELLO.C
EOF
  rm -f "$TMP/dos.png"
  xvfb-run -a -s "-screen 0 1024x768x24" sh -c "
    dosbox -conf '$conf' >/tmp/dos-help.log 2>&1 &
    pid=\$!
    sleep 5.5
    wid=\$(xdotool search --name DOSBox 2>/dev/null | head -1)
    if [ -n \"\$wid\" ]; then
      import -window \"\$wid\" '$TMP/dos.png' 2>/dev/null || true
    fi
    [ -f '$TMP/dos.png' ] || scrot -z '$TMP/dos.png' 2>/dev/null || true
    kill \$pid 2>/dev/null
    wait \$pid 2>/dev/null || true
  " || true
  if [ -f "$TMP/dos.png" ]; then
    frame "$TMP/dos.png" "$HELP/help-dos.png" --nearest
  fi
fi

if [ -d "$GAL" ]; then
  cp -f "$HELP"/help-*.png "$GAL/" 2>/dev/null || true
  echo "  → copied help-*.png to $GAL"
fi

rm -rf "$TMP"

echo "== verify =="
python3 - <<'PY'
from PIL import Image
import os

def check(path):
    im = Image.open(path).convert("RGB")
    px = im.load()
    w, h = im.size
    # status blue near bottom third
    blue = 0
    for y in range(int(h * 0.75), h, 2):
        for x in range(0, w, 4):
            r, g, b = px[x, y]
            if b > 140 and b > r + 30 and g > 80:
                blue += 1
    lit = sum(1 for y in range(0, h, 4) for x in range(0, w, 4) if sum(px[x, y]) > 90)
    # orange help border
    orange = sum(
        1
        for y in range(0, min(h, 400), 2)
        for x in range(0, min(w, 700), 2)
        if px[x, y][0] > 180 and 80 < px[x, y][1] < 190 and px[x, y][2] < 120
    )
    ok = "OK" if (blue > 80 and lit > 1500) or ("dos" in path and lit > 800) else "WEAK"
    # help panels should show orange or enough UI
    if "help-" in os.path.basename(path) and "dos" not in path:
        if orange < 80 and blue < 80:
            ok = "WEAK"
    print(f"  {ok:4} {os.path.basename(path):28} {w}x{h} blue={blue:5} orange={orange:5} lit={lit:5}")

base = "/home/syfaren/Projects/mote/demo-media/help"
for f in sorted(os.listdir(base)):
    if f.endswith(".png"):
        check(os.path.join(base, f))
PY
