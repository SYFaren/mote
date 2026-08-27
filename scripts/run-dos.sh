#!/bin/sh
# Run mote DOS build in DOSBox with a clean mount (no case-twin binaries).
set -eu
ROOT="${ROOT:-$HOME/Projects/mote}"
RUN="${TMPDIR:-/tmp}/mote-dos-run"
export PATH="${HOME}/.local/opt/djgpp/bin:$PATH"

make -C "$ROOT/overlay/dos"

rm -rf "$RUN"
mkdir -p "$RUN"
# IMPORTANT: only one casing of the exe name. On a case-sensitive host
# DOSBox can otherwise pick a stale mote.exe twin over MOTE.EXE.
cp -f "$ROOT/overlay/dos/build/mote.exe" "$RUN/MOTE.EXE"
if [ -f "$ROOT/dist-release/by-platform/dos/CWSDPMI.EXE" ]; then
  cp -f "$ROOT/dist-release/by-platform/dos/CWSDPMI.EXE" "$RUN/"
fi
cp -f "$ROOT/examples/hello_mote.c" "$RUN/HELLO.C" 2>/dev/null || \
  printf '%s\n' 'int main(void){ return 0; }' > "$RUN/HELLO.C"

cat > "$RUN/dosbox.conf" <<EOF
[sdl]
fullscreen=false
autolock=false
output=opengl
[dosbox]
machine=svga_s3
memsize=32
[cpu]
cycles=max
[autoexec]
mount C $RUN
C:
MOTE.EXE HELLO.C
EOF

pkill -f "dosbox -conf $RUN/dosbox.conf" 2>/dev/null || true
DISPLAY="${DISPLAY:-:0}" exec dosbox -conf "$RUN/dosbox.conf"
