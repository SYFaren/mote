# mote/mote/windows — developer: SYFaren
# Fully static PE — no MinGW DLLs.
WINCC ?= x86_64-w64-mingw32-gcc
UPX ?= $(HOME)/.local/opt/upx/upx

CORE = src/core/buffer.c src/core/utf8.c src/core/undo.c
EDIT = src/editor/editor.c
PLAT = src/platform/win32.c
SRC = src/main.c $(CORE) $(EDIT) $(PLAT) src/common/config.c

INCLUDES = -Isrc -Isrc/common -Isrc/core -Isrc/editor -Isrc/platform
CFLAGS = -std=c99 -Wall -Wextra $(INCLUDES) -Os -DNDEBUG -D_WIN32 -mwindows \
	-ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -fno-ident -fomit-frame-pointer -fno-stack-protector \
	-fmerge-all-constants -flto
LDFLAGS = -static -static-libgcc -Os -flto \
	-Wl,--gc-sections -Wl,--strip-all -s -Wl,--build-id=none \
	-lgdi32 -luser32 -lkernel32

.PHONY: all clean pack

all: build/mote.exe

build/mote.exe: $(SRC) | build
	$(WINCC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

pack: build/mote.exe
	@command -v $(UPX) >/dev/null 2>&1 || { echo "UPX missing: $(UPX)"; exit 1; }
	cp -f build/mote.exe build/mote.packed.exe
	$(UPX) --best --lzma -f build/mote.packed.exe
	ls -la build/mote.exe build/mote.packed.exe

build:
	mkdir -p build

clean:
	rm -rf build
