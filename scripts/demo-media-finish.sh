#!/bin/sh
# Finish remaining demo videos with hard timeouts (local only).
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
OUT="${DEMO_OUT:-$ROOT/demo-media}"
VID="$OUT/videos"
DEMO="$ROOT/examples/hello_mote.c"
mkdir -p "$VID"
cd "$ROOT"
export PATH="$HOME/.local/opt/djgpp/bin:$PATH"

record() {
  stem="$1"; shift
  mp4="$VID/$stem.mp4"
  echo "  recording $stem ..."
  timeout 45 xvfb-run -a -s "-screen 0 1024x768x24" sh -c '
    '"$*"' &
    app=$!
    sleep 1.2
    xdotool search --name mote windowactivate 2>/dev/null || \
      xdotool search --class xterm windowactivate 2>/dev/null || \
      xdotool search --name DOSBox windowactivate 2>/dev/null || true
    sleep 0.3
    timeout 18 ffmpeg -y -f x11grab -video_size 1024x768 -framerate 12 -i "$DISPLAY" \
      -t 14 -pix_fmt yuv420p -an "'"$mp4"'" >/tmp/ff-'"$stem"'.log 2>&1 &
    rec=$!
    sleep 0.4
    xdotool key --clearmodifiers F1 || true; sleep 1.0
    xdotool key Escape || true; sleep 0.4
    xdotool key ctrl+t || true; sleep 0.8
    xdotool key ctrl+t || true; sleep 0.8
    xdotool key ctrl+t || true; sleep 0.5
    xdotool key End || true; sleep 0.15
    xdotool key Return || true; sleep 0.15
    xdotool type --delay 30 "// highlight demo" || true
    sleep 0.5
    xdotool key F7 || true; sleep 0.5
    xdotool key F1 || true; sleep 0.8
    xdotool key Escape || true; sleep 0.3
    wait $rec 2>/dev/null || true
    kill $app 2>/dev/null || true
    wait $app 2>/dev/null || true
  ' || true
  if [ -f "$mp4" ] && [ -s "$mp4" ]; then
    echo "  ok $stem.mp4 ($(du -h "$mp4" | awk '{print $1}'))"
  else
    echo "  fail $stem"
  fi
}

# Skip if already good
need() { [ ! -f "$VID/$1.mp4" ] || [ ! -s "$VID/$1.mp4" ]; }

need linux-console && record linux-console \
  xterm -geometry 100x32 -fa Monospace -fs 12 -bg black -fg white -e ./overlay/console/build/mote "$DEMO"

if [ -x overlay/win32/build/mote.exe ]; then
  need windows-gui && record windows-gui wine overlay/win32/build/mote.exe -g 720x420 "$DEMO"
fi

if [ -x overlay/winconsole/build/mote.exe ]; then
  make -C overlay/winconsole >/dev/null
  need windows-console && record windows-console \
    wineconsole --backend=user overlay/winconsole/build/mote.exe -g 100x32 "$DEMO"
fi

if [ -x overlay/dos/build/mote.exe ]; then
  conf=/tmp/mote-dos-vid.conf
  cat > "$conf" <<EOF
[sdl]
fullscreen=false
output=surface
autolock=false
[cpu]
cycles=10000
[autoexec]
mount c $ROOT/overlay/dos/build
mount r $ROOT/overlay/dos/runtime
mount e $ROOT/examples
c:
copy r:\\CWSDPMI.EXE . >nul
mote.exe e:\\hello_mote.c
EOF
  need dos && record dos dosbox -conf "$conf"
fi

# Retake help stills that look empty / refresh with MOTE_START_HELP for GUI
HELP="$OUT/help"
mkdir -p "$HELP"
pad() { convert "$1" -gravity center -background '#0e0e0e' -extent 960x600 "$2" 2>/dev/null || cp "$1" "$2"; }

# X11/SDL help via env
for pair in "linux-x11:./overlay/x11/build/mote" "linux-sdl2:./overlay/sdl/build/mote"; do
  name=${pair%%:*}; bin=${pair#*:}
  raw=/tmp/help-$name.png
  timeout 20 xvfb-run -a -s "-screen 0 1024x768x24" sh -c "
    MOTE_START_HELP=1 $bin -g 720x420 '$DEMO' &
    pid=\$!; sleep 1.3
    xdotool search --name mote windowactivate 2>/dev/null || true
    scrot -u -z '$raw' 2>/dev/null || scrot -z '$raw'
    kill \$pid 2>/dev/null
  " || true
  [ -f "$raw" ] && pad "$raw" "$HELP/help-$name.png" && echo "  refreshed help-$name"
done

# Wayland help dump
raw=/tmp/help-wl.ppm
timeout 25 xvfb-run -a -s "-screen 0 1024x768x24" sh -c "
  weston --backend=x11-backend.so --width=900 --height=560 --socket=mote-wl-h >/tmp/wlh.log 2>&1 &
  w=\$!; sleep 1.6
  WAYLAND_DISPLAY=mote-wl-h MOTE_START_HELP=1 MOTE_DUMP_FB='$raw' \
    ./overlay/wayland/build/mote -g 720x420 '$DEMO' >/dev/null 2>&1 &
  m=\$!; sleep 2.0
  kill \$m \$w 2>/dev/null
" || true
[ -f "$raw" ] && convert "$raw" -gravity center -background '#0e0e0e' -extent 960x600 \
  "$HELP/help-linux-wayland.png" && echo "  refreshed help-wayland"

# Copy help into site gallery as extras (optional browse)
GAL="${SITE_DIR:-$HOME/Projects/mote-site}/gallery"
if [ -d "$GAL" ]; then
  for f in "$HELP"/help-*.png; do
    [ -f "$f" ] || continue
    cp -f "$f" "$GAL/$(basename "$f")"
  done
  echo "  copied help stills → $GAL"
fi

# Update gallery console/dos/winconsole main shots if we can
echo "== refresh platform stills =="
timeout 20 xvfb-run -a -s "-screen 0 1000x700x24" sh -c "
  xterm -geometry 100x32 -fa Monospace -fs 12 -bg black -fg white -e ./overlay/console/build/mote '$DEMO' &
  pid=\$!; sleep 1.4
  xdotool search --class xterm windowactivate 2>/dev/null || true
  scrot -u -z /tmp/plat-console.png 2>/dev/null || scrot -z /tmp/plat-console.png
  kill \$pid 2>/dev/null
" || true
[ -f /tmp/plat-console.png ] && convert /tmp/plat-console.png -gravity center -background '#0e0e0e' -extent 960x600 \
  "$GAL/plat-linux-console.png" && echo "  plat-linux-console.png"

# DOS still
if [ -x overlay/dos/build/mote.exe ]; then
  conf=/tmp/mote-dos-shot.conf
  cat > "$conf" <<EOF
[sdl]
fullscreen=false
output=surface
autolock=false
[cpu]
cycles=8000
[autoexec]
mount c $ROOT/overlay/dos/build
mount r $ROOT/overlay/dos/runtime
mount e $ROOT/examples
c:
copy r:\\CWSDPMI.EXE . >nul
mote.exe e:\\hello_mote.c
EOF
  timeout 25 xvfb-run -a -s "-screen 0 900x700x24" sh -c "
    dosbox -conf '$conf' >/tmp/dos-shot.log 2>&1 &
    pid=\$!; sleep 3.2
    xdotool search --name DOSBox windowactivate 2>/dev/null || true
    scrot -u -z /tmp/plat-dos.png 2>/dev/null || scrot -z /tmp/plat-dos.png
    xdotool key alt+F4 2>/dev/null || kill \$pid 2>/dev/null
  " || true
  if [ -f /tmp/plat-dos.png ]; then
    convert /tmp/plat-dos.png -bordercolor black -border 1 -fuzz 10% -trim +repage \
      -gravity center -background '#0e0e0e' -extent 960x600 "$GAL/plat-dos.png" 2>/dev/null || \
      convert /tmp/plat-dos.png -gravity center -background '#0e0e0e' -extent 960x600 "$GAL/plat-dos.png"
    echo "  plat-dos.png"
  fi
fi

ls -la "$VID" "$HELP"
echo done
