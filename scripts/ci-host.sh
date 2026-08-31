#!/bin/sh
# Resolve CC and make(1) for the current host (Linux, macOS, BSD).
# shellcheck source=targets.sh
target_cc() {
  cross="$1"
  if command -v "${cross}gcc" >/dev/null 2>&1; then
    printf '%sgcc' "$cross"
  elif command -v "${cross}cc" >/dev/null 2>&1; then
    printf '%scc' "$cross"
  elif command -v "${cross}clang" >/dev/null 2>&1; then
    printf '%sclang' "$cross"
  else
    printf '%sgcc' "$cross"
  fi
}

target_make() {
  case "$(uname -s 2>/dev/null)" in
    FreeBSD|OpenBSD|NetBSD)
      if command -v gmake >/dev/null 2>&1; then
        printf 'gmake'
      else
        printf 'make'
      fi
      ;;
    *)
      if command -v gmake >/dev/null 2>&1 && ! make --version 2>/dev/null | grep -qi gnu; then
        printf 'gmake'
      else
        printf 'make'
      fi
      ;;
  esac
}
