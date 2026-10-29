#!/bin/sh
set -eu

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
cd "$ROOT"

make ansi-check
make test
make posix

if command -v "${WINCC:-x86_64-w64-mingw32-gcc}" >/dev/null 2>&1; then
  make windows
else
  echo "skip Windows console build (MinGW not installed)"
fi

overlay/console/build/mote --version
if [ -f overlay/winconsole/build/mote.exe ]; then
  if command -v wine >/dev/null 2>&1; then
    wine overlay/winconsole/build/mote.exe --version
  else
    echo "skip Windows runtime check (Wine not installed)"
  fi
fi

echo "smoke OK"
