# mote overlays

The editor core is shared and compiled with one platform overlay at link time.
The currently maintained overlays are:

| Overlay | Target | Output | Build |
|---|---|---|---|
| `console/` | POSIX TTY (Linux tested) | terminal cells | `make posix` / `make console` |
| `winconsole/` | Windows console | Windows console cells | `make windows` / `make winconsole` |

Each console overlay supplies its filesystem/config hooks. `winconsole/` also
contains the UTF-8 path and native-key helpers needed by its Windows console
backend; no Windows GUI overlay is maintained.

`core.mk` lists the shared core sources. `evq.h` contains the event-queue helper
used by console overlays. `plat/platform.h` defines the contract every overlay
implements. The current maintainer focus is POSIX and Windows console; the
overlay boundary remains available for adding other backends later.
