#!/bin/sh
# Automated DOSBox keyboard smoke for mote (Backspace + a few controls).
set -eu
ROOT="${ROOT:-$HOME/Projects/mote}"
RUN="${TMPDIR:-/tmp}/mote-dos-keys-$$"
export PATH="${HOME}/.local/opt/djgpp/bin:$PATH"

need() { command -v "$1" >/dev/null || { echo "need $1" >&2; exit 1; }; }
need dosbox
need xdotool

make -C "$ROOT/overlay/dos" >/dev/null

rm -rf "$RUN"
mkdir -p "$RUN"
cp -f "$ROOT/overlay/dos/build/mote.exe" "$RUN/MOTE.EXE"
cp -f "$ROOT/dist-release/by-platform/dos/CWSDPMI.EXE" "$RUN/CWSDPMI.EXE" 2>/dev/null \
  || cp -f "$ROOT/overlay/dos/CWSDPMI.EXE" "$RUN/CWSDPMI.EXE" 2>/dev/null \
  || true
# Single casing only — DOSBox+Linux otherwise picks a stale mote.exe twin.
printf '%s\n' 'abcdef' > "$RUN/DEMO.C"

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
MOTE.EXE DEMO.C
EOF

pkill -f "dosbox -conf $RUN/dosbox.conf" 2>/dev/null || true
rm -f "$RUN/KEYTRACE.LOG"
DISPLAY="${DISPLAY:-:0}" dosbox -conf "$RUN/dosbox.conf" >/tmp/mote-dos-keys.log 2>&1 &
DPID=$!

WIN=
i=0
while [ "$i" -lt 40 ]; do
  i=$((i + 1))
  sleep 0.25
  WIN=$(xdotool search --name DOSBox 2>/dev/null | tail -1 || true)
  [ -n "$WIN" ] && break
done
[ -n "$WIN" ] || { echo "DOSBox window not found"; kill "$DPID" 2>/dev/null || true; exit 1; }

xdotool windowactivate --sync "$WIN"
sleep 0.8
# Move to end of first line content then backspace thrice
xdotool key --delay 120 End
xdotool key --delay 120 BackSpace BackSpace BackSpace
xdotool key --delay 120 Escape
xdotool key --delay 80 q
sleep 1.2

kill "$DPID" 2>/dev/null || true
wait "$DPID" 2>/dev/null || true

echo "=== KEYTRACE.LOG ==="
cat "$RUN/KEYTRACE.LOG" 2>/dev/null || { echo "missing KEYTRACE.LOG"; exit 1; }

grep -q 'action=backspace' "$RUN/KEYTRACE.LOG" || { echo "FAIL: no backspace action"; exit 1; }
grep -q 'platkey=.*' "$RUN/KEYTRACE.LOG" || { echo "FAIL: no platkey lines"; exit 1; }
# PK_BACKSPACE must appear (enum value — print numeric). Accept platkey line after backspace.
awk '
  /action=backspace/ { want=1; next }
  want && /platkey=/ { ok=1; exit }
  END { exit ok ? 0 : 1 }
' "$RUN/KEYTRACE.LOG" || { echo "FAIL: backspace did not emit platkey"; exit 1; }

echo "DOS key smoke OK"
rm -rf "$RUN"
