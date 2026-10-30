# Build

The active development targets are the POSIX terminal and Windows console. The
shared core and `Plat` interface remain overlay-based so another backend can be
added later without redesigning the editor core.

## Requirements

| Target | Command | Requirements |
|---|---|---|
| POSIX terminal (Linux tested) | `make posix` (or `make console`) | C compiler, GNU make, TTY to run |
| Windows console | `make windows` (or `make winconsole`) | MinGW-w64, default compiler `x86_64-w64-mingw32-gcc` |

Override compiler commands when needed:

```sh
make posix CC=cc
make windows WINCC=x86_64-w64-mingw32-gcc
```

## Tests

```sh
make ansi-check   # shared core must compile as ANSI C89
make test         # core/editor tests and terminal escape decoding
make smoke        # checks the two active builds (Windows/Wine optional)
```

The terminal escape test uses a pseudo-terminal; `make test` may report it
skipped if the host has no `script` utility or cannot allocate a TTY.

## Release build

```sh
make release
```

This builds and tests the POSIX terminal and Windows console release artifacts
(the POSIX build is currently verified on Linux):

```text
dist-release/mote-posix-console
dist-release/mote-windows-console.exe
```

Tagged GitHub releases are built by `.github/workflows/release.yml`. The release
workflow publishes these binaries and `SHA256SUMS`. This is the current
maintainer target set, not a restriction on the overlay architecture; future
overlays can be added as separate targets and CI jobs.

## Recovery platform hooks

The shared `core/app/recovery.c` is included by `overlay/core.mk`. Each overlay
must implement `plat_lock_file()` (exclusive, nonblocking session lock released
by `fclose()` or process exit) and `plat_private_write()` (private recovery temp
file creation). POSIX uses `flock` and mode `0600`; Windows uses an exclusive
file handle and inherits the configuration directory ACL. Lock files must not
be unlinked on exit. Recovery snapshots are local plaintext data, not release
artifacts; keep runtime tests isolated with a temporary configuration directory.
`make recovery-test` runs POSIX PTY crash/restore and concurrent-instance checks
with Python 3 and an isolated temporary configuration directory.
