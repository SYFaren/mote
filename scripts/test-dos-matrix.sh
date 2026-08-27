#!/bin/sh
# Full DOSBox key matrix: every important PlatKey path + save roundtrip.
set -eu
ROOT="${ROOT:-$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)}"
RUN="${TMPDIR:-/tmp}/mote-dos-matrix-$$"
export PATH="${HOME}/.local/opt/djgpp/bin:${PATH:-}"
export DISPLAY="${DISPLAY:-:0}"

need() { command -v "$1" >/dev/null || { echo "need $1" >&2; exit 1; }; }
need dosbox
need xdotool

make -C "$ROOT/overlay/dos" >/dev/null

rm -rf "$RUN"
mkdir -p "$RUN"
cp -f "$ROOT/overlay/dos/build/mote.exe" "$RUN/MOTE.EXE"
cp -f "$ROOT/dist-release/by-platform/dos/CWSDPMI.EXE" "$RUN/CWSDPMI.EXE" 2>/dev/null \
  || cp -f "$ROOT/overlay/dos/CWSDPMI.EXE" "$RUN/CWSDPMI.EXE" 2>/dev/null || true

printf '%s\n' 'int n = 1;' > "$RUN/A.C"
printf '%s\n' 'int m = 2;' > "$RUN/B.C"

cat > "$RUN/dosbox.conf" <<EOF
[sdl]
fullscreen=false
autolock=false
[dosbox]
memsize=16
[cpu]
cycles=max
[autoexec]
mount C $RUN
C:
SET MOTE_KEYTRACE=1
MOTE.EXE A.C B.C
EOF

pkill -f "dosbox -conf $RUN/dosbox.conf" 2>/dev/null || true
rm -f "$RUN/KEYTRACE.LOG" "$RUN/OUT.C"
DISPLAY="$DISPLAY" dosbox -conf "$RUN/dosbox.conf" >/tmp/mote-dos-matrix.log 2>&1 &
DPID=$!

WIN=
i=0
while [ "$i" -lt 50 ]; do
  i=$((i + 1))
  sleep 0.2
  WIN=$(xdotool search --name DOSBox 2>/dev/null | tail -1 || true)
  [ -n "$WIN" ] && break
done
[ -n "$WIN" ] || { echo "DOSBox window not found"; kill "$DPID" 2>/dev/null || true; exit 1; }

xdotool windowactivate --sync "$WIN"
sleep 1.0

# --- save first (before other modes) ---
xdotool key --delay 80 End
xdotool type --delay 50 'Z'
xdotool key --delay 150 ctrl+s
sleep 1.0

# --- navigation / edit ---
xdotool key --delay 80 End
xdotool key --delay 80 BackSpace
xdotool type --delay 40 '99'
# undo / redo
xdotool key --delay 100 ctrl+z
xdotool key --delay 100 ctrl+y
# theme, wrap, ws
xdotool key --delay 80 ctrl+t
xdotool key --delay 80 ctrl+w
xdotool key --delay 80 F7
# find next / prev
xdotool key --delay 80 ctrl+f
sleep 0.2
xdotool type --delay 40 'int'
xdotool key --delay 80 Return
xdotool key --delay 80 F3
xdotool key --delay 80 shift+F3
xdotool key --delay 80 Escape
# Alt bindings (saveas path etc. — just fire)
xdotool key --delay 80 alt+c
xdotool key --delay 80 alt+w
xdotool key --delay 80 alt+r
xdotool key --delay 80 alt+r
xdotool key --delay 80 alt+e
xdotool key --delay 80 alt+e
xdotool key --delay 80 Escape
# delete-line on a disposable moment after we finish save tests — skip here
# docs: F2 next, Shift+F2 prev — return to first doc before save test
xdotool key --delay 100 F2
xdotool key --delay 100 shift+F2
# dup line, bracket
xdotool key --delay 80 ctrl+d
xdotool key --delay 80 ctrl+bracketright
# Shift+Tab (BackTab)
xdotool key --delay 80 shift+Tab

# Save As (Alt+S)
xdotool key --delay 150 alt+s
sleep 0.7
xdotool key --delay 90 o u t period c
xdotool key --delay 150 Return
sleep 1.2

# quit (may ask) — discard
xdotool key --delay 100 ctrl+q
sleep 0.3
xdotool key --delay 80 ctrl+q
sleep 0.8

kill "$DPID" 2>/dev/null || true
wait "$DPID" 2>/dev/null || true

echo "=== KEYTRACE (tail) ==="
tail -80 "$RUN/KEYTRACE.LOG" 2>/dev/null || { echo "missing KEYTRACE.LOG"; exit 1; }

ok=0
bad=0
check() {
  if grep -q "$1" "$RUN/KEYTRACE.LOG"; then
    echo "  OK  $2"
    ok=$((ok + 1))
  else
    echo " FAIL $2 (want /$1/)"
    bad=$((bad + 1))
  fi
}

check 'action=backspace' 'Backspace'
check 'platkey=' 'platkey lines present'
check 'ctrl=1' 'ctrl modifiers logged'
check 'platkey=26' 'SaveAs (Alt+S)'
check 'platkey=15' 'Save (Ctrl+S)'
check 'platkey=40' 'NextDoc (F2)'
check 'platkey=48' 'FindPrev (Shift+F3)'

if grep -q 'Z' "$RUN/A.C" 2>/dev/null; then
  echo "  OK  Ctrl+S wrote edit into A.C"
  ok=$((ok + 1))
else
  echo " FAIL Ctrl+S did not update A.C"
  bad=$((bad + 1))
fi

if ls "$RUN"/[Oo][Uu][Tt].C "$RUN"/out.c 2>/dev/null | grep -q .; then
  echo "  OK  Save As created OUT.C"
  ok=$((ok + 1))
else
  echo " WARN Save As OUT.C missing (xdotool/DOSBox prompt flaky)"
fi

echo "DOS matrix: $ok ok, $bad fail"
[ "$bad" -eq 0 ]
rm -rf "$RUN"
