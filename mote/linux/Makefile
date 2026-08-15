# mote/mote/linux — developer: SYFaren
CC ?= gcc
UPX ?= $(HOME)/.local/opt/upx/upx

CORE = src/core/buffer.c src/core/utf8.c src/core/undo.c
EDIT = src/editor/editor.c
PLAT = src/platform/x11.c
SRC = src/main.c $(CORE) $(EDIT) $(PLAT) src/common/config.c

INCLUDES = -Isrc -Isrc/common -Isrc/core -Isrc/editor -Isrc/platform
CFLAGS = -std=c99 -Wall -Wextra $(INCLUDES) -Os -DNDEBUG \
	-ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -fno-ident -fomit-frame-pointer -fno-stack-protector \
	-fno-plt -fmerge-all-constants -flto
LDFLAGS = -lX11 -Os -flto -Wl,--gc-sections -Wl,--as-needed -s \
	-Wl,--build-id=none -Wl,-z,norelro

.PHONY: all clean run test pack

all: build/mote

build/mote: $(SRC) | build
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

build/test_core: src/test/test_core.c $(CORE) | build
	$(CC) $(CFLAGS) -O0 -g -o $@ src/test/test_core.c $(CORE)

test: build/test_core
	./build/test_core

# Packed copy for distribution (original build/mote stays for debug)
pack: build/mote
	@command -v $(UPX) >/dev/null 2>&1 || { echo "UPX missing: $(UPX)"; exit 1; }
	cp -f build/mote build/mote.packed
	$(UPX) --best --lzma -f build/mote.packed
	ls -la build/mote build/mote.packed

build:
	mkdir -p build

run: build/mote
	./build/mote

clean:
	rm -rf build
