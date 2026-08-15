# mote (Linux)

Independent Linux codebase. Developer: **SYFaren**

## Layout

```
src/
  main.c
  common/     shared constants
  core/       gap buffer, utf8, undo
  editor/     editing UI logic
  platform/   X11 backend (x11.c)
  test/       core self-tests
```

## Build / run

```sh
cd mote/linux
make
./build/mote
make test
make pack          # UPX → build/mote.packed
```

Needs: `gcc`, `libx11-dev`. UPX optional (`~/.local/opt/upx`).

## Help

- In app: **F1** / **Ctrl+H**
- [HELP](HELP)
