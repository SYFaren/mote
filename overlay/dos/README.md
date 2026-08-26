# mote DOS overlay (DJGPP)

Needs [build-djgpp](https://github.com/andrewwutw/build-djgpp/releases) on `PATH`:

```sh
export PATH="$HOME/.local/opt/djgpp/bin:$PATH"
make dos
# → overlay/dos/build/mote.exe
```

Run under FreeDOS or DOSBox (CWSDPMI / go32). VGA 80×25 text, keys like GUI (Ctrl+S/Q/F…, F1 help). Config: `MOTE\CONFIG`. Max file size 2 MiB in this build.
