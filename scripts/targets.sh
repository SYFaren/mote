#!/bin/sh
# Cross/toolchain prefixes for build-port.sh ( POSIX sh, source only )

target_cross() {
  os="$1"
  arch="$2"
  case "$os-$arch" in
    linux-amd64)   printf '' ;;
    linux-i686)    printf 'i686-linux-gnu-' ;;
    linux-arm64)   printf 'aarch64-linux-gnu-' ;;
    linux-armhf)   printf 'arm-linux-gnueabihf-' ;;
    linux-riscv64) printf 'riscv64-linux-gnu-' ;;
    windows-amd64) printf 'x86_64-w64-mingw32-' ;;
    windows-i686)  printf 'i686-w64-mingw32-' ;;
    macos-amd64|macos-arm64)
      if [ "$(uname -s 2>/dev/null)" = "Darwin" ]; then
        printf ''
      else
        echo "error: macos builds require a Darwin host" >&2
        return 1
      fi
      ;;
    freebsd-amd64|openbsd-amd64|netbsd-amd64)
      if [ "$(uname -s 2>/dev/null)" = "FreeBSD" ] || \
         [ "$(uname -s 2>/dev/null)" = "OpenBSD" ] || \
         [ "$(uname -s 2>/dev/null)" = "NetBSD" ]; then
        printf ''
      else
        echo "error: $os builds require a $os host (native cc)" >&2
        return 1
      fi
      ;;
    freebsd-arm64)
      if [ "$(uname -s 2>/dev/null)" = "FreeBSD" ]; then
        printf ''
      else
        echo "error: freebsd arm64 requires FreeBSD host" >&2
        return 1
      fi
      ;;
    dos-i686)
      if command -v i586-pc-msdosdjgpp-gcc >/dev/null 2>&1; then
        printf 'i586-pc-msdosdjgpp-'
      else
        echo "error: DJGPP not on PATH (~/.local/opt/djgpp/bin)" >&2
        return 1
      fi
      ;;
    *)
      echo "error: unsupported target $os-$arch" >&2
      return 1
      ;;
  esac
}

# pkg-config search path for Linux cross builds (multiarch dev packages).
target_pkglibdir() {
  _arch="$1"
  case "$_arch" in
    arm64)   printf '/usr/lib/aarch64-linux-gnu/pkgconfig:/usr/share/pkgconfig' ;;
    armhf)   printf '/usr/lib/arm-linux-gnueabihf/pkgconfig:/usr/share/pkgconfig' ;;
    i686)    printf '/usr/lib/i386-linux-gnu/pkgconfig:/usr/share/pkgconfig' ;;
    riscv64) printf '/usr/lib/riscv64-linux-gnu/pkgconfig:/usr/share/pkgconfig' ;;
    *)       printf '' ;;
  esac
}

# qemu-user binary for smoke-testing cross Linux ELFs on the build host.
target_qemu() {
  _arch="$1"
  case "$_arch" in
    arm64)   printf 'qemu-aarch64-static' ;;
    armhf)   printf 'qemu-arm-static' ;;
    i686)    printf 'qemu-i386-static' ;;
    riscv64) printf 'qemu-riscv64-static' ;;
    *)       printf '' ;;
  esac
}
