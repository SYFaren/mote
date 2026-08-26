#!/bin/sh
# Help screenshots + usage demo videos (local only — not for the site).
# Output: demo-media/help/*.png  demo-media/videos/*.mp4
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
OUT="${DEMO_OUT:-$ROOT/demo-media}"
HELP="$OUT/help"
VID="$OUT/videos"
DEMO="$ROOT/examples/hello_mote.c"
TMP="${TMPDIR:-/tmp}/mote-demo-$$"
mkdir -p "$HELP" "$VID" "$TMP"
cd "$ROOT"

need() { command -v "$1" >/dev/null 2>&1; }

echo "== build =="
make -C overlay/x11 >/dev/null
make -C overlay/sdl >/dev/null
make -C overlay/console >/dev/null
make -C overlay/wayland >/dev/null || true
make -C overlay/win32 >/dev/null || true
make -C overlay/winconsole >/dev/null || true
make -C overlay/dos >/dev/null || true

pad960() {
  src="$1" dst="$2"
  if need convert; then
    convert "$src" -gravity center -background '#0e0e0e' -extent 960x600 "$dst"
  else
    cp -f "$src" "$dst"
  fi
}

# --- Help stills (F1) -------------------------------------------------------
shot_help_xvfb() {
  # $1 name  rest=command
  name="$1"; shift
  raw="$TMP/help-$name.png"
  rm -f "$raw"
  if ! need xvfb-run || ! need scrot || ! need xdotool; then
    echo "  skip help-$name (need xvfb+scrot+xdotool)"
    return 0
  fi
  xvfb-run -a -s "-screen 0 1024x768x24" sh -c "
    $* &
    pid=\$!
    sleep 1.2
    xdotool search --name mote windowactivate --sync 2>/dev/null || true
    sleep 0.2
    xdotool key F1
    sleep 0.5
    scrot -u -z '$raw' 2>/dev/null || scrot -z '$raw'
    kill \$pid 2>/dev/null || true
    wait \$pid 2>/dev/null || true
  " || true
  if [ -f "$raw" ]; then
    pad960 "$raw" "$HELP/help-$name.png"
    echo "  help-$name.png"
  else
    echo "  skip help-$name (no capture)"
  fi
}

echo "== help screenshots → $HELP =="
shot_help_xvfb linux-x11 ./overlay/x11/build/mote -g 720x420 "$DEMO"
shot_help_xvfb linux-sdl2 ./overlay/sdl/build/mote -g 720x420 "$DEMO"

if need xvfb-run && need xterm && need scrot && need xdotool; then
  raw="$TMP/help-console.png"
  xvfb-run -a -s "-screen 0 1000x700x24" sh -c "
    xterm -geometry 100x32 -fa Monospace -fs 12 -bg black -fg white -e \
      ./overlay/console/build/mote '$DEMO' &
    pid=\$!
    sleep 1.4
    xdotool search --class xterm windowactivate --sync 2>/dev/null || true
    xdotool key F1
    sleep 0.5
    scrot -u -z '$raw' 2>/dev/null || scrot -z '$raw'
    kill \$pid 2>/dev/null || true
  " || true
  [ -f "$raw" ] && pad960 "$raw" "$HELP/help-linux-console.png" && \
    echo "  help-linux-console.png" || echo "  skip help-console"
fi

if need weston && need xvfb-run; then
  raw="$TMP/help-wayland.ppm"
  rm -f "$raw"
  xvfb-run -a -s "-screen 0 1024x768x24" sh -c "
    weston --backend=x11-backend.so --width=900 --height=560 --socket=mote-wl-help \
      >/tmp/weston-help.log 2>&1 &
    echo \$! > '$TMP/w.pid'
    sleep 1.8
    WAYLAND_DISPLAY=mote-wl-help MOTE_START_HELP=1 MOTE_DUMP_FB='$raw' \
      ./overlay/wayland/build/mote -g 720x420 '$DEMO' >/tmp/wl-help.log 2>&1 &
    echo \$! > '$TMP/m.pid'
    sleep 2.0
    kill \$(cat '$TMP/m.pid') \$(cat '$TMP/w.pid') 2>/dev/null || true
  " || true
  if [ -f "$raw" ] && need convert; then
    convert "$raw" -gravity center -background '#0e0e0e' -extent 960x600 \
      "$HELP/help-linux-wayland.png"
    echo "  help-linux-wayland.png"
  else
    echo "  skip help-wayland"
  fi
fi

if need wine && [ -x overlay/win32/build/mote.exe ]; then
  shot_help_xvfb windows-gui wine overlay/win32/build/mote.exe -g 720x420 "$DEMO"
fi

if need wine && [ -x overlay/winconsole/build/mote.exe ]; then
  raw="$TMP/help-winconsole.png"
  xvfb-run -a -s "-screen 0 1024x768x24" sh -c "
    wineconsole --backend=user overlay/winconsole/build/mote.exe -g 100x32 '$DEMO' &
    pid=\$!
    sleep 2.0
    xdotool search --name mote windowactivate --sync 2>/dev/null || \
      xdotool search --class wineconsole windowactivate --sync 2>/dev/null || true
    xdotool key F1
    sleep 0.6
    scrot -u -z '$raw' 2>/dev/null || scrot -z '$raw'
    kill \$pid 2>/dev/null || true
  " || true
  [ -f "$raw" ] && pad960 "$raw" "$HELP/help-windows-console.png" && \
    echo "  help-windows-console.png" || echo "  skip help-winconsole"
fi

# DOS help via dosbox + screenshot of window
if need dosbox && [ -x overlay/dos/build/mote.exe ]; then
  raw="$TMP/help-dos.png"
  conf="$TMP/dos-help.conf"
  cat > "$conf" <<EOF
[sdl]
fullscreen=false
fulldouble=false
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
  xvfb-run -a -s "-screen 0 900x700x24" sh -c "
    dosbox -conf '$conf' >/tmp/dos-help.log 2>&1 &
    pid=\$!
    sleep 3.5
    xdotool search --name 'DOSBox' windowactivate --sync 2>/dev/null || true
    xdotool key F1
    sleep 0.8
    scrot -u -z '$raw' 2>/dev/null || scrot -z '$raw'
    xdotool key alt+F4 2>/dev/null || kill \$pid 2>/dev/null || true
  " || true
  [ -f "$raw" ] && pad960 "$raw" "$HELP/help-dos.png" && echo "  help-dos.png" || \
    echo "  skip help-dos"
fi

# --- Demo videos -------------------------------------------------------------
# Shared key choreography (xdotool key names):
#   F1 help → Esc → Ctrl+T theme → Ctrl+T → type comment → F7 ws → Ctrl+G goto
demo_keys() {
  # assume window already focused
  sleep 0.8
  xdotool key F1; sleep 1.2
  xdotool key Escape; sleep 0.5
  xdotool key ctrl+t; sleep 1.0
  xdotool key ctrl+t; sleep 1.0
  xdotool key ctrl+t; sleep 0.8
  xdotool key End; sleep 0.2
  xdotool key Return; sleep 0.2
  xdotool type --delay 40 '// theme + highlight demo'
  sleep 0.8
  xdotool key F7; sleep 0.8
  xdotool key ctrl+g; sleep 0.4
  xdotool type 5; sleep 0.3
  xdotool key Return; sleep 1.0
  xdotool key F1; sleep 1.0
  xdotool key Escape; sleep 0.5
}

record_xvfb_app() {
  # $1 out stem  rest=launch cmd
  stem="$1"; shift
  mp4="$VID/$stem.mp4"
  if ! need xvfb-run || ! need ffmpeg || ! need xdotool; then
    echo "  skip video $stem"
    return 0
  fi
  echo "  recording $stem ..."
  xvfb-run -a -s "-screen 0 1024x768x24" sh -c "
    $* &
    app=\$!
    sleep 1.3
    xdotool search --name mote windowactivate --sync 2>/dev/null || true
    ffmpeg -y -f x11grab -video_size 1024x768 -framerate 15 -i \"\$DISPLAY\" \
      -t 14 -pix_fmt yuv420p -an '$mp4' >/tmp/ff-$stem.log 2>&1 &
    rec=\$!
    sleep 0.5
    $(type demo_keys | sed '1d;$d')
    wait \$rec 2>/dev/null || true
    kill \$app 2>/dev/null || true
    wait \$app 2>/dev/null || true
  " || true
  if [ -f "$mp4" ] && [ -s "$mp4" ]; then
    echo "  ok $stem.mp4 ($(du -h "$mp4" | cut -f1))"
  else
    echo "  fail $stem"
  fi
}

echo "== demo videos → $VID =="
# Inline record helper (avoid nested function export issues)
record_one() {
  stem="$1"; shift
  mp4="$VID/$stem.mp4"
  echo "  recording $stem ..."
  xvfb-run -a -s "-screen 0 1024x768x24" sh -c '
    '"$*"' &
    app=$!
    sleep 1.4
    xdotool search --name mote windowactivate --sync 2>/dev/null || \
      xdotool search --class xterm windowactivate --sync 2>/dev/null || true
    ffmpeg -y -f x11grab -video_size 1024x768 -framerate 12 -i "$DISPLAY" \
      -t 16 -pix_fmt yuv420p -an "'"$mp4"'" >/tmp/ff-'"$stem"'.log 2>&1 &
    rec=$!
    sleep 0.6
    xdotool key F1; sleep 1.2
    xdotool key Escape; sleep 0.5
    xdotool key ctrl+t; sleep 1.0
    xdotool key ctrl+t; sleep 1.0
    xdotool key ctrl+t; sleep 0.7
    xdotool key End; sleep 0.2
    xdotool key Return; sleep 0.2
    xdotool type --delay 35 "// theme + highlight demo"
    sleep 0.7
    xdotool key F7; sleep 0.7
    xdotool key ctrl+g; sleep 0.3
    xdotool type 5; sleep 0.2
    xdotool key Return; sleep 0.8
    xdotool key F1; sleep 1.0
    xdotool key Escape; sleep 0.4
    wait $rec 2>/dev/null || true
    kill $app 2>/dev/null || true
    wait $app 2>/dev/null || true
  ' || true
  if [ -f "$mp4" ] && [ -s "$mp4" ]; then
    echo "  ok $stem.mp4 ($(du -h "$mp4" | awk "{print \$1}"))"
  else
    echo "  fail $stem (see /tmp/ff-$stem.log)"
  fi
}

if need xvfb-run && need ffmpeg && need xdotool; then
  record_one linux-x11 ./overlay/x11/build/mote -g 720x420 "$DEMO"
  record_one linux-sdl2 ./overlay/sdl/build/mote -g 720x420 "$DEMO"
  record_one linux-console xterm -geometry 100x32 -fa Monospace -fs 12 -bg black -fg white -e ./overlay/console/build/mote "$DEMO"
  if [ -x overlay/win32/build/mote.exe ] && need wine; then
    record_one windows-gui wine overlay/win32/build/mote.exe -g 720x420 "$DEMO"
  fi
  if [ -x overlay/winconsole/build/mote.exe ] && need wine; then
    record_one windows-console wineconsole --backend=user overlay/winconsole/build/mote.exe -g 100x32 "$DEMO"
  fi
  # DOSBox
  if need dosbox && [ -x overlay/dos/build/mote.exe ]; then
    conf="$TMP/dos-vid.conf"
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
    mp4="$VID/dos.mp4"
    echo "  recording dos ..."
    xvfb-run -a -s "-screen 0 900x700x24" sh -c "
      dosbox -conf '$conf' >/tmp/dos-vid.log 2>&1 &
      app=\$!
      sleep 3.5
      xdotool search --name 'DOSBox' windowactivate --sync 2>/dev/null || true
      ffmpeg -y -f x11grab -video_size 900x700 -framerate 12 -i \"\$DISPLAY\" \
        -t 16 -pix_fmt yuv420p -an '$mp4' >/tmp/ff-dos.log 2>&1 &
      rec=\$!
      sleep 0.5
      xdotool key F1; sleep 1.2
      xdotool key Escape; sleep 0.5
      xdotool key ctrl+t; sleep 1.0
      xdotool key ctrl+t; sleep 1.0
      xdotool key ctrl+t; sleep 0.7
      xdotool key End; sleep 0.2
      xdotool key Return; sleep 0.2
      xdotool type --delay 40 '// dos highlight'
      sleep 0.8
      xdotool key F7; sleep 0.8
      xdotool key F1; sleep 1.0
      xdotool key Escape; sleep 0.4
      wait \$rec 2>/dev/null || true
      xdotool key alt+F4 2>/dev/null || kill \$app 2>/dev/null || true
    " || true
    [ -f "$mp4" ] && [ -s "$mp4" ] && echo "  ok dos.mp4" || echo "  fail dos"
  fi
else
  echo "  skip videos (need xvfb+ffmpeg+xdotool)"
fi

# README index
cat > "$OUT/README.md" <<EOF
# mote demo media (local)

Not published to the website. Regenerated by \`scripts/demo-media.sh\`.

## Help stills
See \`help/\` — F1 overlay on each platform.

## Videos
See \`videos/\` — short usage demos (~16s):
help → themes (Ctrl+T) → type → whitespace (F7) → goto → help again.

EOF

echo "== done → $OUT =="
ls -la "$HELP" "$VID" 2>/dev/null || true
rm -rf "$TMP"
