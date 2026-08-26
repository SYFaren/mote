#!/bin/sh
# Capture clean per-platform screenshots → mote-site/gallery/plat-*.png
# Crops to editor content (no huge black desktop padding).
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
SITE="${SITE_DIR:-$HOME/Projects/mote-site}"
GAL="$SITE/gallery"
DEMO="$ROOT/examples/hello_mote.c"
TMP="${TMPDIR:-/tmp}/mote-shots-$$"
mkdir -p "$GAL" "$TMP"
cd "$ROOT"

need() { command -v "$1" >/dev/null 2>&1; }

# Crop near-black desktop padding; keep editor chrome.
crop_content() {
  src="$1" dst="$2"
  if need convert; then
    convert "$src" -bordercolor black -border 1 -fuzz 8% -trim +repage \
      -bordercolor '#101010' -border 8 "$dst" 2>/dev/null || cp -f "$src" "$dst"
  else
    cp -f "$src" "$dst"
  fi
}

shot_xvfb_app() {
  # $1 out name (plat-….png), rest = command
  out="$GAL/$1"
  shift
  raw="$TMP/raw-$1.png"
  raw="$TMP/raw.png"
  rm -f "$raw"
  if ! need xvfb-run || ! need scrot; then
    echo "  skip $out (need xvfb-run+scrot)"
    return 0
  fi
  xvfb-run -a -s "-screen 0 1024x768x24" sh -c "
    $* &
    pid=\$!
    sleep 1.4
    if command -v xdotool >/dev/null 2>&1; then
      xdotool search --name mote windowactivate --sync 2>/dev/null || true
      sleep 0.2
      scrot -u -z '$raw' 2>/dev/null || scrot -z '$raw'
    else
      scrot -z '$raw'
    fi
    kill \$pid 2>/dev/null || true
    wait \$pid 2>/dev/null || true
  " || true
  if [ -f "$raw" ]; then
    crop_content "$raw" "$out"
    echo "  shot $(basename "$out") ($(identify -format '%wx%h' "$out" 2>/dev/null || echo ok))"
  else
    echo "  skip $out (no capture)"
  fi
}

echo "== screenshots → $GAL =="

make -C overlay/x11 >/dev/null
make -C overlay/sdl >/dev/null
make -C overlay/console >/dev/null
make -C overlay/wayland >/dev/null
make -C overlay/win32 >/dev/null
make -C overlay/winconsole >/dev/null

shot_xvfb_app plat-linux-x11.png \
  ./overlay/x11/build/mote -g 720x420 "$DEMO"

shot_xvfb_app plat-linux-sdl2.png \
  ./overlay/sdl/build/mote -g 720x420 "$DEMO"

# console in xterm
if need xvfb-run && need xterm && need scrot; then
  raw="$TMP/console.png"
  xvfb-run -a -s "-screen 0 900x600x24" sh -c "
    xterm -geometry 100x32 -fa Monospace -fs 11 -bg black -fg white -e \
      ./overlay/console/build/mote '$DEMO' &
    pid=\$!
    sleep 1.5
    if command -v xdotool >/dev/null 2>&1; then
      xdotool search --class xterm windowactivate --sync 2>/dev/null || true
      scrot -u -z '$raw' 2>/dev/null || scrot -z '$raw'
    else
      scrot -z '$raw'
    fi
    kill \$pid 2>/dev/null || true
    wait \$pid 2>/dev/null || true
  " || true
  [ -f "$raw" ] && crop_content "$raw" "$GAL/plat-linux-console.png" && \
    echo "  shot plat-linux-console.png" || echo "  skip console"
fi

# Wayland nested: GL/Xvfb scrot often invents vertical stripes. Prefer soft-FB dump.
if need weston && need xvfb-run; then
  raw="$TMP/wayland.ppm"
  rm -f "$raw"
  xvfb-run -a -s "-screen 0 1024x768x24" sh -c "
    weston --backend=x11-backend.so --width=900 --height=560 --socket=mote-wl-shot \
      >/tmp/weston-shot.log 2>&1 &
    echo \$! > '$TMP/weston.pid'
    sleep 1.8
    WAYLAND_DISPLAY=mote-wl-shot MOTE_DUMP_FB='$raw' \
      ./overlay/wayland/build/mote -g 720x420 '$DEMO' >/tmp/wl-mote.log 2>&1 &
    echo \$! > '$TMP/mote.pid'
    sleep 2.0
    kill \$(cat '$TMP/mote.pid') \$(cat '$TMP/weston.pid') 2>/dev/null || true
  " || true
  if [ -f "$raw" ] && need convert; then
    convert "$raw" -gravity center -background '#0e0e0e' -extent 960x600 \
      "$GAL/plat-linux-wayland.png"
    echo "  shot plat-linux-wayland.png (fb dump)"
  else
    echo "  skip wayland"
  fi
fi

# Windows GUI (wine)
if need wine && [ -x overlay/win32/build/mote.exe ]; then
  shot_xvfb_app plat-windows-gui.png \
    wine overlay/win32/build/mote.exe -g 720x420 "$DEMO"
fi

# Windows console
if need wine && [ -x overlay/winconsole/build/mote.exe ]; then
  raw="$TMP/winconsole.png"
  if need xvfb-run && need scrot; then
    xvfb-run -a -s "-screen 0 1024x768x24" sh -c "
      wineconsole --backend=user overlay/winconsole/build/mote.exe '$DEMO' &
      sleep 3
      scrot -z '$raw'
      pkill -f 'mote.exe' 2>/dev/null || true
      pkill -f wineconsole 2>/dev/null || true
    " || true
    [ -f "$raw" ] && crop_content "$raw" "$GAL/plat-windows-console.png" && \
      echo "  shot plat-windows-console.png" || echo "  skip winconsole"
  fi
fi

# DOS via DOSBox if present
if need dosbox && [ -x overlay/dos/build/mote.exe ]; then
  raw="$TMP/dos.png"
  mkdir -p "$TMP/dos"
  cp -f overlay/dos/build/mote.exe "$DEMO" "$TMP/dos/" 2>/dev/null || true
  cp -f "$DEMO" "$TMP/dos/HELLO.C"
  if need xvfb-run && need scrot; then
    xvfb-run -a -s "-screen 0 800x600x24" sh -c "
      dosbox -conf /dev/null -c 'MOUNT C $TMP/dos' -c 'C:' -c 'mote.exe HELLO.C' \
        >/tmp/dosbox.log 2>&1 &
      sleep 4
      scrot -z '$raw'
      pkill -f dosbox 2>/dev/null || true
    " || true
    [ -f "$raw" ] && crop_content "$raw" "$GAL/plat-dos.png" && \
      echo "  shot plat-dos.png" || echo "  skip dos"
  fi
fi

# WASM — real browser after fix
if [ -f overlay/wasm/build/mote.html ] && need google-chrome; then
  raw="$TMP/wasm.png"
  (
    cd overlay/wasm/build
    python3 -m http.server 8765 >/tmp/mote-wasm-http.log 2>&1 &
    echo $! > "$TMP/http.pid"
  )
  sleep 0.6
  if need xvfb-run && need scrot; then
    xvfb-run -a -s "-screen 0 1100x800x24" sh -c "
      google-chrome --disable-gpu --window-size=1000,700 --window-position=0,0 \
        --app=http://127.0.0.1:8765/mote.html >/tmp/chrome-wasm.log 2>&1 &
      sleep 5
      if command -v xdotool >/dev/null; then
        xdotool mousemove 500 400 click 1
        sleep 1.5
      fi
      scrot -z '$raw'
      pkill -f 'chrome.*8765' 2>/dev/null || pkill -f google-chrome 2>/dev/null || true
    " || true
  fi
  kill "$(cat "$TMP/http.pid")" 2>/dev/null || true
  if [ -f "$raw" ]; then
    crop_content "$raw" "$GAL/plat-web-wasm.png"
    echo "  shot plat-web-wasm.png"
  else
    echo "  skip wasm"
  fi
fi

rm -rf "$TMP"
echo "done:"
ls -la "$GAL"/plat-*.png 2>/dev/null || true
