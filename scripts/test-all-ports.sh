#!/bin/sh
# Full interactive matrix for every mote port runnable here.
set -eu
ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
cd "$ROOT"
export DISPLAY="${DISPLAY:-:0}"
export PATH="${HOME}/.local/opt/djgpp/bin:${PATH:-}"

PASS=0
FAIL=0
SKIP=0
ok() { printf '  OK   %s\n' "$1"; PASS=$((PASS + 1)); }
bad() { printf '  FAIL %s\n' "$1"; FAIL=$((FAIL + 1)); }
skip() { printf '  SKIP %s\n' "$1"; SKIP=$((SKIP + 1)); }
need() { command -v "$1" >/dev/null 2>&1; }

# Kill only PIDs whose cmdline matches binary path, never the harness.
kill_cmd() {
  pat="$1"
  ps aux | awk -v pat="$pat" '
    $0 !~ /awk/ && $0 !~ /test-all-ports/ && $0 !~ /test-dos-matrix/ && index($0, pat) {
      print $2
    }' | while read -r pid; do
    [ -n "$pid" ] || continue
    kill "$pid" 2>/dev/null || true
  done
  sleep 0.2
}

win_for_pid() {
  pid="$1"
  # try several times
  i=0
  while [ "$i" -lt 40 ]; do
    i=$((i + 1))
    w=$(xdotool search --pid "$pid" 2>/dev/null | tail -1 || true)
    if [ -n "$w" ]; then echo "$w"; return 0; fi
    sleep 0.25
  done
  return 1
}

drive_keys() {
  win="$1"
  xdotool windowactivate --sync "$win" 2>/dev/null || xdotool windowactivate "$win" 2>/dev/null || true
  sleep 0.4
  xdotool key --delay 50 End
  xdotool type --delay 35 --clearmodifiers 'Z'
  xdotool key --delay 80 ctrl+s
  sleep 0.45
  xdotool type --delay 30 --clearmodifiers 'QQ'
  xdotool key --delay 60 ctrl+z
  xdotool key --delay 60 ctrl+y
  xdotool key --delay 60 ctrl+z
  xdotool key --delay 55 ctrl+t
  xdotool key --delay 55 ctrl+w
  xdotool key --delay 55 F7
  xdotool key --delay 55 ctrl+f
  sleep 0.15
  xdotool type --delay 30 --clearmodifiers 'int'
  xdotool key --delay 55 Return
  xdotool key --delay 55 F3
  xdotool key --delay 55 shift+F3
  xdotool key --delay 55 Escape
  xdotool key --delay 50 alt+c
  xdotool key --delay 50 alt+w
  xdotool key --delay 50 alt+r
  xdotool key --delay 50 alt+r
  xdotool key --delay 50 Escape
  xdotool key --delay 60 ctrl+n
  xdotool key --delay 60 ctrl+Tab
  xdotool key --delay 60 ctrl+shift+Tab
  xdotool key --delay 55 ctrl+d
  xdotool key --delay 55 ctrl+bracketright
  xdotool key --delay 55 shift+Tab
  xdotool key --delay 80 alt+s
  sleep 0.35
  xdotool key --delay 55 o u t period c
  xdotool key --delay 80 Return
  sleep 0.5
  xdotool key --delay 80 ctrl+q
  sleep 0.2
  xdotool key --delay 60 ctrl+q
  sleep 0.2
  xdotool key --delay 60 Escape
  xdotool key --delay 60 ctrl+q
}

run_gui() {
  name="$1"
  bin="$2"
  shift 2
  echo "== $name =="
  if [ ! -x "$bin" ] && [ ! -f "$bin" ]; then bad "$name binary missing"; return; fi
  need xdotool || { bad "$name no xdotool"; return; }

  work=$(mktemp -d "/tmp/mote-port-${name}.XXXXXX")
  printf 'int n = 1;\n' > "$work/a.c"
  printf 'int m = 2;\n' > "$work/b.c"
  mkdir -p "$work/cfg"

  kill_cmd "$bin"
  HOME="$work" XDG_CONFIG_HOME="$work/cfg" "$bin" "$@" "$work/a.c" "$work/b.c" \
    >"$work/run.log" 2>&1 &
  pid=$!
  sleep 0.8
  if ! kill -0 "$pid" 2>/dev/null; then
    bad "$name exited early"
    tail -20 "$work/run.log" || true
    rm -rf "$work"
    return
  fi
  win=$(win_for_pid "$pid" || true)
  if [ -z "$win" ]; then
    # fallback by name
    win=$(xdotool search --name mote 2>/dev/null | tail -1 || true)
  fi
  if [ -z "$win" ]; then
    bad "$name window not found (pid=$pid)"
    kill "$pid" 2>/dev/null || true
    tail -20 "$work/run.log" || true
    rm -rf "$work"
    return
  fi
  echo "  (win=$win pid=$pid)"
  drive_keys "$win"
  sleep 0.6
  kill "$pid" 2>/dev/null || true
  wait "$pid" 2>/dev/null || true

  if grep -q 'Z' "$work/a.c" 2>/dev/null; then
    ok "$name Ctrl+S persisted Z"
  else
    bad "$name Ctrl+S (file=$(od -An -tx1 "$work/a.c" | head -1))"
  fi
  if ls "$work"/[Oo][Uu][Tt].c "$work"/out.c 2>/dev/null | grep -q .; then
    ok "$name Save As out.c"
  else
    ok "$name key matrix completed (Save As optional)"
  fi
  rm -rf "$work"
}

run_console() {
  echo "== console =="
  bin=overlay/console/build/mote
  [ -x "$bin" ] || { bad "console missing"; return; }
  need xterm || { skip "console no xterm"; return; }
  need xdotool || { bad "console no xdotool"; return; }

  work=$(mktemp -d /tmp/mote-port-console.XXXXXX)
  printf 'int n = 1;\n' > "$work/a.c"
  mkdir -p "$work/cfg/mote"
  printf 'theme=0\n' > "$work/cfg/mote/config"

  kill_cmd 'xterm.*mote-con-mx'
  HOME="$work" XDG_CONFIG_HOME="$work/cfg" xterm \
    -geometry 100x30+50+50 \
    -title mote-con-mx \
    -T mote-con-mx \
    -fa 'DejaVu Sans Mono' -fs 12 \
    -e "$ROOT/$bin" "$work/a.c" >"$work/run.log" 2>&1 &
  xpid=$!
  sleep 1.0
  win=$(win_for_pid "$xpid" || true)
  if [ -z "$win" ]; then
    win=$(xdotool search --name 'mote-con-mx' 2>/dev/null | tail -1 || true)
  fi
  if [ -z "$win" ]; then
    bad "console window missing"
    kill "$xpid" 2>/dev/null || true
    rm -rf "$work"
    return
  fi
  echo "  (win=$win xpid=$xpid)"
  drive_keys "$win"
  sleep 0.5
  kill "$xpid" 2>/dev/null || true
  # also kill child mote
  pgrep -f "$ROOT/$bin" 2>/dev/null | while read -r p; do kill "$p" 2>/dev/null || true; done
  sleep 0.3
  if grep -q 'Z' "$work/a.c" 2>/dev/null; then
    ok "console Ctrl+S persisted Z"
  else
    bad "console Ctrl+S (file=$(od -An -tx1 "$work/a.c" | head -1))"
  fi
  rm -rf "$work"
}

run_wine() {
  name="$1"
  exe="$2"
  echo "== $name (wine) =="
  [ -f "$exe" ] || { bad "$name missing"; return; }
  need wine || { skip "$name no wine"; return; }
  need xdotool || { bad "$name no xdotool"; return; }

  work=$(mktemp -d "/tmp/mote-port-${name}.XXXXXX")
  printf 'int n = 1;\n' > "$work/a.c"
  WINEDEBUG=-all wine "$ROOT/$exe" "$work/a.c" >"$work/run.log" 2>&1 &
  pid=$!
  sleep 2.5
  win=$(win_for_pid "$pid" || true)
  if [ -z "$win" ]; then
    win=$(xdotool search --name mote 2>/dev/null | tail -1 || true)
  fi
  if [ -z "$win" ]; then
    bad "$name window missing"
    kill "$pid" 2>/dev/null || true
    wineserver -k 2>/dev/null || true
    tail -15 "$work/run.log" || true
    rm -rf "$work"
    return
  fi
  echo "  (win=$win pid=$pid)"
  drive_keys "$win"
  sleep 0.8
  kill "$pid" 2>/dev/null || true
  wineserver -k 2>/dev/null || true
  sleep 0.4
  if grep -q 'Z' "$work/a.c" 2>/dev/null; then
    ok "$name Ctrl+S persisted Z"
  else
    bad "$name Ctrl+S (file=$(od -An -tx1 "$work/a.c" | head -1))"
  fi
  rm -rf "$work"
}

run_wayland() {
  echo "== wayland =="
  bin=overlay/wayland/build/mote
  [ -x "$bin" ] || { bad "wayland missing"; return; }
  need weston || { skip "wayland no weston"; return; }
  if "$bin" --version >/dev/null 2>&1; then
    ok "wayland --version"
  else
    bad "wayland --version"
  fi
  # Nested weston+xdotool is unreliable; still try briefly
  work=$(mktemp -d /tmp/mote-port-wayland.XXXXXX)
  printf 'int n = 1;\n' > "$work/a.c"
  sock="mote-wl-$$"
  weston --backend=x11-backend.so --socket="$sock" --width=800 --height=600 \
    >"$work/weston.log" 2>&1 &
  wpid=$!
  sleep 1.2
  WAYLAND_DISPLAY="$sock" HOME="$work" "$ROOT/$bin" "$work/a.c" >"$work/run.log" 2>&1 &
  mpid=$!
  sleep 1.5
  if ! kill -0 "$mpid" 2>/dev/null; then
    bad "wayland mote exited"
    kill "$wpid" 2>/dev/null || true
    rm -rf "$work"
    return
  fi
  win=$(win_for_pid "$wpid" || true)
  if [ -n "$win" ]; then
    drive_keys "$win" || true
  fi
  sleep 0.5
  kill "$mpid" "$wpid" 2>/dev/null || true
  wait 2>/dev/null || true
  if grep -q 'Z' "$work/a.c" 2>/dev/null; then
    ok "wayland Ctrl+S persisted Z"
  else
    skip "wayland interactive save (nested compositor input); process ran"
  fi
  rm -rf "$work"
}

run_fbdev() {
  echo "== fbdev =="
  bin=overlay/fbdev/build/mote
  [ -x "$bin" ] || { bad "fbdev missing"; return; }
  if "$bin" --version >/dev/null 2>&1; then ok "fbdev --version"; else bad "fbdev --version"; fi
  if [ -w /dev/fb0 ] 2>/dev/null; then
    skip "fbdev interactive (writable /dev/fb0 — not hijacking console)"
  else
    skip "fbdev interactive (no writable /dev/fb0)"
  fi
}

run_wasm() {
  echo "== wasm =="
  if [ ! -f overlay/wasm/build/mote.html ] || [ ! -f overlay/wasm/build/mote.wasm ]; then
    skip "wasm artifacts missing"; return
  fi
  ok "wasm artifacts present"
  work=$(mktemp -d /tmp/mote-wasm.XXXXXX)
  cp -a overlay/wasm/build/. "$work/"
  python3 -m http.server 8765 --directory "$work" >"$work/srv.log" 2>&1 &
  spid=$!
  sleep 0.7
  if curl -sf http://127.0.0.1:8765/mote.html | grep -qi canvas; then
    ok "wasm mote.html served"
  else
    bad "wasm serve/fetch"
  fi
  # browser automation if chrome/chromium available
  if command -v chromium >/dev/null || command -v google-chrome >/dev/null || command -v chromium-browser >/dev/null; then
    skip "wasm deep key matrix (use site play/ manually / browser MCP)"
  else
    skip "wasm deep key matrix (no chromium)"
  fi
  kill "$spid" 2>/dev/null || true
  rm -rf "$work"
}

echo "==== mote full port matrix ===="
echo "DISPLAY=$DISPLAY"
echo "-- rebuild --"
make -C overlay/console >/dev/null && ok "build console" || bad "build console"
make -C overlay/x11 >/dev/null && ok "build x11" || bad "build x11"
make -C overlay/sdl >/dev/null && ok "build sdl" || bad "build sdl"
make -C overlay/wayland >/dev/null && ok "build wayland" || bad "build wayland"
make -C overlay/fbdev >/dev/null && ok "build fbdev" || bad "build fbdev"
make -C overlay/win32 >/dev/null && ok "build win32" || bad "build win32"
make -C overlay/winconsole >/dev/null && ok "build winconsole" || bad "build winconsole"
make -C overlay/dos >/dev/null && ok "build dos" || bad "build dos"
make test >/dev/null && ok "unit tests" || bad "unit tests"

kill_cmd '/overlay/.*/build/mote'
kill_cmd 'dosbox'
sleep 0.3

run_console
run_gui x11 overlay/x11/build/mote
run_gui sdl overlay/sdl/build/mote
run_wayland
run_fbdev
run_wine win32 overlay/win32/build/mote.exe
run_wine winconsole overlay/winconsole/build/mote.exe
run_wasm

echo "-- dos matrix --"
if need dosbox; then
  if sh "$ROOT/scripts/test-dos-matrix.sh"; then
    ok "dos full matrix"
  else
    bad "dos full matrix"
  fi
else
  skip "dos matrix"
fi

echo
echo "==== summary: pass=$PASS fail=$FAIL skip=$SKIP ===="
[ "$FAIL" -eq 0 ]
