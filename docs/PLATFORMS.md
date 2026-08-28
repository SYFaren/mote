# mote — platform & architecture matrix

Naming for release binaries:

```text
mote-<os>-<arch>-<backend>[.exe|.upx…]
```

Examples: `mote-linux-arm64-console`, `mote-freebsd-amd64-x11`, `mote-windows-i686-gui.exe`.

Legacy flat names (`mote-linux-console`, …) stay as **linux amd64** aliases.

## OS × architecture (CPU)

| OS | amd64 / x86_64 | i686 / x386 | arm64 / aarch64 | armhf / armv7 | riscv64 | notes |
|----|----------------|-------------|-----------------|---------------|---------|-------|
| **Linux** | ✅ tier 1 | ✅ tier 2 | ✅ tier 1 | ✅ tier 2 | ⚪ tier 3 | primary host; cross from Debian |
| **FreeBSD** | ✅ tier 1 | ⚪ tier 3 | ✅ tier 2 | — | ⚪ tier 3 | native build on FreeBSD |
| **OpenBSD** | ✅ tier 2 | — | ⚪ tier 3 | — | — | native build on OpenBSD |
| **NetBSD** | ✅ tier 2 | — | ⚪ tier 3 | — | — | native build on NetBSD |
| **Windows** | ✅ tier 1 | ✅ tier 2 | ⚪ tier 3 | — | — | MinGW cross from Linux |
| **DOS** | — | ✅ tier 1 | — | — | — | DJGPP i586 only |
| **Web** | — | — | — | — | — | wasm (arch-neutral) |

Legend: **tier 1** = release asset; **tier 2** = supported via `make release-cross`; **tier 3** = planned / on demand.

## Backend × OS (what actually ports)

| Backend | Linux | FreeBSD | OpenBSD | NetBSD | Windows | DOS | wasm |
|---------|-------|---------|---------|--------|---------|-----|------|
| **console** (TTY) | ✅ | ✅ | ✅ | ✅ | ✅ winconsole | — | — |
| **x11** | ✅ | ✅ | ✅ | ✅ | — | — | — |
| **sdl2** | ✅ | ✅ | ⚪ | ⚪ | ✅ | — | ✅ |
| **wayland** | ✅ | — | — | — | — | — | — |
| **fbdev** | ✅ | — | — | — | — | — | — |
| **win32 GUI** | — | — | — | — | ✅ | — | — |
| **dos** | — | — | — | — | — | ✅ | — |

BSD builds use the same POSIX/console and X11 code; Linux-only pieces (`fbdev`, VT `KDSKBMODE`, Wayland) are skipped.

## Cross-compiler prefixes (Linux host)

| Target | `CROSS` prefix | Debian package |
|--------|----------------|----------------|
| linux amd64 | *(native gcc)* | build-essential |
| linux i686 | `i686-linux-gnu-` | gcc-i686-linux-gnu |
| linux arm64 | `aarch64-linux-gnu-` | gcc-aarch64-linux-gnu |
| linux armhf | `arm-linux-gnueabihf-` | gcc-arm-linux-gnueabihf |
| linux riscv64 | `riscv64-linux-gnu-` | gcc-riscv64-linux-gnu |
| windows amd64 | `x86_64-w64-mingw32-` | gcc-mingw-w64-x86-64 |
| windows i686 | `i686-w64-mingw32-` | gcc-mingw-w64-i686 |

BSD **amd64/arm64** binaries are built **on the BSD system** (`make release-bsd`) unless a cross toolchain is installed separately.

## Build commands

```sh
# Current host (linux amd64): same as before
make release-linux

# Linux × arch cross-build (console + x11; sdl if libs exist)
make release-cross-linux

# On FreeBSD / OpenBSD / NetBSD machine:
make release-bsd

# Everything the host can produce
make release
```

Single port:

```sh
sh scripts/build-port.sh linux arm64 console
sh scripts/build-port.sh freebsd amd64 console   # on FreeBSD
sh scripts/build-port.sh windows i686 gui
```

Outputs land under:

```text
dist-release/by-platform/<os>/<arch>/<backend>/
dist-release/flat/mote-<os>-<arch>-<backend>
```

## What will **not** run where

- Linux ELF (`mote-linux-*`) → **not** on BSD/macOS natively (different libc/loader).
- BSD ELF → **not** on Linux.
- Same OS, different arch → **not** without the matching CPU (no amd64 binary on arm64).

Install the binary built for **your OS + CPU**, or build from source on the machine.
