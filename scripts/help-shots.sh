#!/bin/sh
# Capture F1/help overlays → demo-media/help/ (+ copy to mote-site/gallery)
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
HELP="$ROOT/demo-media/help"
GAL="${SITE_DIR:-$HOME/Projects/mote-site}/gallery"
DEMO="$ROOT/examples/hello_mote.c"
mkdir -p "$HELP"
cd "$ROOT"
export PATH="${HOME}/.local/opt/djgpp/bin:${PATH:-}"

echo "== build =="
make -C overlay/x11 >/dev/null
make -C overlay/sdl >/dev/null
make -C overlay/console >/dev/null
make -C overlay/wayland >/dev/null
make -C overlay/win32 >/dev/null 2>/dev/null || true
make -C overlay/winconsole >/dev/null 2>/dev/null || true
make -C overlay/dos >/dev/null 2>/dev/null || true

pad() {
  convert "$1" -gravity center -background '#0e0e0e' -extent 960x600 "$2"
}

crop_content_pad() {
  python3 - "$1" "$2" <<'PY'
import sys
from PIL import Image
src, dst = sys.argv[1], sys.argv[2]
im = Image.open(src).convert("RGB")
w, h = im.size
px = im.load()
xs, ys = [], []
for y in range(0, h, 1):
    for x in range(0, w, 1):
        if sum(px[x, y]) > 28:
            xs.append(x); ys.append(y)
if not xs:
    im.save(dst); raise SystemExit
x0, x1 = max(0, min(xs) - 4), min(w, max(xs) + 5)
y0, y1 = max(0, min(ys) - 4), min(h, max(ys) + 5)
crop = im.crop((x0, y0, x1, y1))
canvas = Image.new("RGB", (960, 600), (14, 14, 14))
cw, ch = crop.size
if cw > 960 or ch > 600:
    s = min(960 / cw, 600 / ch)
    crop = crop.resize((max(1, int(cw * s)), max(1, int(ch * s))), Image.NEAREST)
    cw, ch = crop.size
canvas.paste(crop, ((960 - cw) // 2, (600 - ch) // 2))
canvas.save(dst)
print("  crop", x1 - x0, "x", y1 - y0, "→", dst)
PY
}

echo "== help shots =="

# SDL soft-FB dump (cleanest)
rm -f /tmp/mote-help-sdl.ppm
xvfb-run -a -s "-screen 0 900x600x24" sh -c "
  MOTE_START_HELP=1 MOTE_DUMP_FB=/tmp/mote-help-sdl.ppm \
    ./overlay/sdl/build/mote -g 720x420 '$DEMO' >/dev/null 2>&1 &
  pid=\$!; sleep 1.4; kill \$pid 2>/dev/null; wait \$pid 2>/dev/null || true
"
convert /tmp/mote-help-sdl.ppm -gravity center -background '#0e0e0e' -extent 960x600 \
  "$HELP/help-linux-sdl2.png"
echo "  help-linux-sdl2.png"

# Wayland soft-FB dump
rm -f /tmp/mote-help-wl.ppm
xvfb-run -a -s "-screen 0 1024x768x24" sh -c "
  weston --backend=x11-backend.so --width=900 --height=560 --socket=mote-help \
    >/tmp/weston-help.log 2>&1 &
  w=\$!; sleep 1.7
  WAYLAND_DISPLAY=mote-help MOTE_START_HELP=1 MOTE_DUMP_FB=/tmp/mote-help-wl.ppm \
    ./overlay/wayland/build/mote -g 720x420 '$DEMO' >/dev/null 2>&1 &
  m=\$!; sleep 2.0; kill \$m \$w 2>/dev/null; wait 2>/dev/null || true
"
convert /tmp/mote-help-wl.ppm -gravity center -background '#0e0e0e' -extent 960x600 \
  "$HELP/help-linux-wayland.png"
echo "  help-linux-wayland.png"

# X11 window
rm -f /tmp/mote-help-x11.png
xvfb-run -a -s "-screen 0 1024x768x24" sh -c "
  MOTE_START_HELP=1 ./overlay/x11/build/mote -g 720x420 '$DEMO' >/dev/null 2>&1 &
  pid=\$!; sleep 1.6
  xdotool search --name mote windowactivate 2>/dev/null || true
  scrot -u -z /tmp/mote-help-x11.png 2>/dev/null || scrot -z /tmp/mote-help-x11.png
  kill \$pid 2>/dev/null; wait \$pid 2>/dev/null || true
"
crop_content_pad /tmp/mote-help-x11.png "$HELP/help-linux-x11.png"

# console
rm -f /tmp/mote-help-con.png
xvfb-run -a -s "-screen 0 1100x750x24" sh -c "
  MOTE_START_HELP=1 xterm -geometry 100x32 -fa 'DejaVu Sans Mono' -fs 13 \
    -bg '#1e1e1e' -fg '#d4d4d4' -e ./overlay/console/build/mote '$DEMO' &
  pid=\$!; sleep 1.7
  xdotool search --class xterm windowactivate 2>/dev/null || true
  scrot -u -z /tmp/mote-help-con.png 2>/dev/null || scrot -z /tmp/mote-help-con.png
  kill \$pid 2>/dev/null; wait \$pid 2>/dev/null || true
"
pad /tmp/mote-help-con.png "$HELP/help-linux-console.png"
echo "  help-linux-console.png"

# Windows GUI
if [ -x overlay/win32/build/mote.exe ]; then
  rm -f /tmp/mote-help-w32.png
  xvfb-run -a -s "-screen 0 1024x768x24" sh -c "
    MOTE_START_HELP=1 wine ./overlay/win32/build/mote.exe -g 720x420 '$DEMO' >/dev/null 2>&1 &
    pid=\$!; sleep 2.2
    xdotool search --name mote windowactivate 2>/dev/null || true
    scrot -u -z /tmp/mote-help-w32.png 2>/dev/null || scrot -z /tmp/mote-help-w32.png
    kill \$pid 2>/dev/null; wait \$pid 2>/dev/null || true
  " || true
  [ -f /tmp/mote-help-w32.png ] && crop_content_pad /tmp/mote-help-w32.png "$HELP/help-windows-gui.png"
fi

# Windows console — import by window id
if [ -x overlay/winconsole/build/mote.exe ]; then
  rm -f /tmp/mote-help-wc.png
  xvfb-run -a -s "-screen 0 1280x800x24" sh -c "
    MOTE_START_HELP=1 wineconsole --backend=user ./overlay/winconsole/build/mote.exe -H -g 100x32 '$DEMO' >/dev/null 2>&1 &
    pid=\$!; sleep 4.5
    wid=\$(xdotool search --name wineconsole 2>/dev/null | head -1)
    [ -z \"\$wid\" ] && wid=\$(xdotool search --name mote 2>/dev/null | head -1)
    if [ -n \"\$wid\" ]; then
      xdotool windowactivate \"\$wid\" 2>/dev/null || true
      sleep 0.3
      import -window \"\$wid\" /tmp/mote-help-wc.png 2>/dev/null || scrot -u -z /tmp/mote-help-wc.png
    else
      scrot -z /tmp/mote-help-wc.png
    fi
    kill \$pid 2>/dev/null; wait \$pid 2>/dev/null || true
  " || true
  if [ -f /tmp/mote-help-wc.png ]; then
    # Wine often yields a blank console; only publish if colors look like text UI.
    if python3 - <<'PY'
from PIL import Image
im = Image.open("/tmp/mote-help-wc.png").convert("RGB")
px, w, h = im.load(), im.size[0], im.size[1]
# Require non-gray content (theme status cyan / help panel), not just window chrome
colorful = sum(
    1
    for y in range(0, h, 2)
    for x in range(0, w, 2)
    if max(px[x, y]) - min(px[x, y]) > 40 and sum(px[x, y]) > 80
)
raise SystemExit(0 if colorful > 400 else 1)
PY
    then
      crop_content_pad /tmp/mote-help-wc.png "$HELP/help-windows-console.png" || \
        pad /tmp/mote-help-wc.png "$HELP/help-windows-console.png"
      echo "  help-windows-console.png"
    else
      echo "  help-windows-console.png skipped (blank under Wine)"
    fi
  fi
fi

# DOS
if [ -x overlay/dos/build/mote.exe ]; then
  conf=/tmp/mote-help-dos.conf
  cat > "$conf" <<EOF
[sdl]
fullscreen=false
output=surface
autolock=false
[cpu]
cycles=25000
[autoexec]
mount c $ROOT/overlay/dos/build
mount r $ROOT/overlay/dos/runtime
mount e $ROOT/examples
c:
copy r:\\CWSDPMI.EXE . >nul
mote.exe -H e:\\hello_mote.c
EOF
  rm -f /tmp/mote-help-dos.png
  xvfb-run -a -s "-screen 0 1024x768x24" sh -c "
    dosbox -conf '$conf' >/tmp/dos-help.log 2>&1 &
    pid=\$!
    sleep 5.5
    wid=\$(xdotool search --name DOSBox 2>/dev/null | head -1)
    if [ -n \"\$wid\" ]; then
      import -window \"\$wid\" /tmp/mote-help-dos.png 2>/dev/null || true
    fi
    [ -f /tmp/mote-help-dos.png ] || scrot -z /tmp/mote-help-dos.png 2>/dev/null || true
    kill \$pid 2>/dev/null
    wait \$pid 2>/dev/null || true
  " || true
  if [ -f /tmp/mote-help-dos.png ]; then
    # Scale 1:1 DOS text up, then letterbox — avoid aggressive trim eating glyphs
    convert /tmp/mote-help-dos.png -filter point -resize 200% \
      -gravity center -background '#0e0e0e' -extent 960x600 "$HELP/help-dos.png"
    echo "  help-dos.png"
  fi
fi

if [ -d "$GAL" ]; then
  cp -f "$HELP"/help-*.png "$GAL/"
  echo "  → copied to $GAL"
fi

echo "== verify =="
python3 - <<'PY'
from PIL import Image
import os
d = "/home/syfaren/Projects/mote/demo-media/help"
for f in sorted(os.listdir(d)):
    if not f.endswith(".png"):
        continue
    im = Image.open(os.path.join(d, f)).convert("RGB")
    px = im.load(); w, h = im.size
    orange = sum(
        1
        for y in range(0, h, 2)
        for x in range(0, w, 2)
        if px[x, y][0] > 200 and 100 < px[x, y][1] < 180 and px[x, y][2] < 100
    )
    panel = sum(
        1
        for y in range(0, min(h, 220), 2)
        for x in range(0, min(w, 520), 2)
        if 18 <= px[x, y][0] <= 45 and 22 <= px[x, y][1] <= 50 and 28 <= px[x, y][2] <= 55
    )
    # DOS VGA / console 16-color: accept any lively non-flat frame
    lit = sum(1 for y in range(0, h, 4) for x in range(0, w, 4) if sum(px[x, y]) > 90)
    uniq = len({px[x, y] for y in range(0, h, 8) for x in range(0, w, 8)})
    blue = sum(
        1
        for y in range(0, h, 2)
        for x in range(0, w, 2)
        if px[x, y][2] > 150 and px[x, y][2] > px[x, y][0] + 40
    )
    ok = "OK" if (orange > 200 or panel > 2000 or blue > 1500 or (lit > 2000 and "dos" in f)) else "WEAK"
    print(f"  {ok:4} {f:28} orange={orange:5} panel={panel:5} blue={blue:5} lit={lit:5}")
PY
