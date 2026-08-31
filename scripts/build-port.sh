#!/bin/sh
# Build one mote port:  build-port.sh <os> <arch> <backend>
# Examples:
#   build-port.sh linux amd64 console
#   build-port.sh linux arm64 x11
#   build-port.sh freebsd amd64 console
#   build-port.sh windows amd64 gui
set -eu

ROOT="$(CDPATH= cd -- "$(dirname "$0")/.." && pwd)"
OS="${1:?os (linux macos freebsd openbsd netbsd windows dos)}"
ARCH="${2:?arch (amd64 i686 arm64 armhf riscv64)}"
BACKEND="${3:?backend (console x11 sdl wayland fbdev gui winconsole dos wasm)}"

DIST="$ROOT/dist-release"
CAT="$DIST/by-platform/$OS/$ARCH"
FLAT="$DIST/flat"

mkdir -p "$CAT" "$FLAT"

# shellcheck source=targets.sh
. "$ROOT/scripts/targets.sh"
# shellcheck source=ci-host.sh
. "$ROOT/scripts/ci-host.sh"

CROSS="$(target_cross "$OS" "$ARCH")" || exit 1
CC="$(target_cc "$CROSS")"
MAKE="$(target_make)"
export CC
export MAKE
export PATH="${HOME}/.local/opt/djgpp/bin:${HOME}/.local/opt/emsdk/upstream/emscripten:${HOME}/.local/opt/emsdk:${PATH}"

MOTE_OS="$OS"
MOTE_ARCH="$ARCH"
export MOTE_OS MOTE_ARCH

if [ -n "$CROSS" ] && [ "$OS" = linux ]; then
  PKGDIR="$(target_pkglibdir "$ARCH")"
  if [ -n "$PKGDIR" ]; then
    export PKG_CONFIG_LIBDIR="$PKGDIR"
  fi
fi

case "$BACKEND" in
  console|x11|sdl|wayland|fbdev)
    MK="$ROOT/overlay/$BACKEND"
    [ "$BACKEND" = sdl ] && MK="$ROOT/overlay/sdl"
    [ -d "$MK" ] || { echo "no overlay: $BACKEND" >&2; exit 1; }
    case "$OS" in
      linux) ;;
      macos)
        case "$BACKEND" in
          console|sdl) ;;
          *) echo "$BACKEND not supported on macos (CI: console + sdl only)" >&2; exit 1 ;;
        esac
        ;;
      freebsd|openbsd|netbsd)
        case "$BACKEND" in
          console|x11|sdl) ;;
          *) echo "$BACKEND not supported on $OS" >&2; exit 1 ;;
        esac
        ;;
      *) echo "$BACKEND requires unix OS, got $OS" >&2; exit 1 ;;
    esac
    if [ -n "$CROSS" ] && [ "$OS" = linux ]; then
      case "$BACKEND" in
        wayland|fbdev)
          echo "$BACKEND cross-build skipped (linux amd64 native only)" >&2
          exit 0
          ;;
      esac
    fi
    "$MAKE" -C "$MK" clean 2>/dev/null || true
    "$MAKE" -C "$MK" CC="$CC" MOTE_OS="$MOTE_OS" MOTE_ARCH="$MOTE_ARCH" all
    if [ "$OS" = linux ] && [ "$ARCH" = amd64 ]; then
      BDIR=build
    else
      BDIR="build-$OS-$ARCH"
    fi
    OUT="$MK/$BDIR/mote"
    FLAT_NAME="mote-$OS-$ARCH-$BACKEND"
    CAT_NAME="$BACKEND"
    [ "$BACKEND" = sdl ] && CAT_NAME=sdl2
    mkdir -p "$CAT/$CAT_NAME"
    cp -f "$OUT" "$CAT/$CAT_NAME/mote"
    cp -f "$OUT" "$FLAT/$FLAT_NAME"
    if [ "$OS" = linux ] && [ "$ARCH" = amd64 ] ]; then
      case "$BACKEND" in
        console) cp -f "$OUT" "$FLAT/mote-linux-console" ;;
        x11) cp -f "$OUT" "$FLAT/mote-linux-x11" ;;
        sdl) cp -f "$OUT" "$FLAT/mote-linux-sdl2" ;;
        wayland) cp -f "$OUT" "$FLAT/mote-linux-wayland" ;;
        fbdev) cp -f "$OUT" "$FLAT/mote-linux-fbdev" ;;
      esac
    fi
    "$MAKE" -C "$MK" CC="$CC" MOTE_OS="$MOTE_OS" MOTE_ARCH="$MOTE_ARCH" pack 2>/dev/null && \
      cp -f "$MK/$BDIR/mote.packed" "$CAT/$CAT_NAME/mote.upx" 2>/dev/null && \
      cp -f "$MK/$BDIR/mote.packed" "$FLAT/$FLAT_NAME.upx" 2>/dev/null || true
    printf '%s\n' "$OS $ARCH · $CAT_NAME" > "$CAT/$CAT_NAME/README.txt"
    echo "ok: $FLAT/$FLAT_NAME"
    ;;
  gui)
    [ "$OS" = windows ] || { echo "gui is windows-only" >&2; exit 1; }
    WINCC="${CROSS}gcc"
    "$MAKE" -C "$ROOT/overlay/win32" clean 2>/dev/null || true
    "$MAKE" -C "$ROOT/overlay/win32" WINCC="$WINCC" all
    mkdir -p "$CAT/gui"
    cp -f "$ROOT/overlay/win32/build/mote.exe" "$CAT/gui/mote.exe"
    cp -f "$ROOT/overlay/win32/build/mote.exe" "$FLAT/mote-$OS-$ARCH-gui.exe"
    [ "$ARCH" = amd64 ] && cp -f "$ROOT/overlay/win32/build/mote.exe" "$FLAT/mote-windows-gui.exe"
    "$MAKE" -C "$ROOT/overlay/win32" pack 2>/dev/null && \
      cp -f "$ROOT/overlay/win32/build/mote.packed.exe" "$CAT/gui/mote.upx.exe" 2>/dev/null && \
      cp -f "$ROOT/overlay/win32/build/mote.packed.exe" "$FLAT/mote-$OS-$ARCH-gui.upx.exe" 2>/dev/null || true
    printf '%s\n' "$OS $ARCH · GDI GUI" > "$CAT/gui/README.txt"
    echo "ok: $FLAT/mote-$OS-$ARCH-gui.exe"
    ;;
  winconsole)
    [ "$OS" = windows ] || { echo "winconsole is windows-only" >&2; exit 1; }
    WINCC="${CROSS}gcc"
    "$MAKE" -C "$ROOT/overlay/winconsole" clean 2>/dev/null || true
    "$MAKE" -C "$ROOT/overlay/winconsole" WINCC="$WINCC" all
    mkdir -p "$CAT/console"
    cp -f "$ROOT/overlay/winconsole/build/mote.exe" "$CAT/console/mote.exe"
    cp -f "$ROOT/overlay/winconsole/build/mote.exe" "$FLAT/mote-$OS-$ARCH-console.exe"
    [ "$ARCH" = amd64 ] && cp -f "$ROOT/overlay/winconsole/build/mote.exe" "$FLAT/mote-windows-console.exe"
    printf '%s\n' "$OS $ARCH · console host" > "$CAT/console/README.txt"
    echo "ok: $FLAT/mote-$OS-$ARCH-console.exe"
    ;;
  dos)
    [ "$OS" = dos ] && [ "$ARCH" = i686 ] || { echo "dos is i686 only" >&2; exit 1; }
    "$MAKE" -C "$ROOT/overlay/dos" clean 2>/dev/null || true
    "$MAKE" -C "$ROOT/overlay/dos" all
    mkdir -p "$CAT"
    cp -f "$ROOT/overlay/dos/build/mote.exe" "$CAT/mote.exe"
    cp -f "$ROOT/overlay/dos/build/mote.exe" "$FLAT/mote-dos.exe"
    echo "ok: $FLAT/mote-dos.exe"
    ;;
  *)
    echo "unknown backend: $BACKEND" >&2
    exit 1
    ;;
esac
