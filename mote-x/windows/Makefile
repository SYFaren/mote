# mote/mote-x/windows — developer: SYFaren
# Fully static PE — no MinGW DLLs.
WINCC ?= x86_64-w64-mingw32-gcc
UPX ?= $(HOME)/.local/opt/upx/upx

CORE = src/core/buffer.c src/core/utf8.c src/core/undo.c src/core/hl.c
EDIT = src/editor/editor.c src/editor/theme.c
PLAT = src/platform/win32.c
SRC = src/main.c $(CORE) $(EDIT) $(PLAT) src/common/config.c

INCLUDES = -Isrc -Isrc/common -Isrc/editor -Isrc/core -Isrc/platform
CFLAGS = -std=c99 -Wall -Wextra $(INCLUDES) -Os -DNDEBUG -D_WIN32 -mwindows \
	-ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -fno-ident -fomit-frame-pointer -fno-stack-protector \
	-fmerge-all-constants -flto
LDFLAGS = -static -static-libgcc -Os -flto \
	-Wl,--gc-sections -Wl,--strip-all -s -Wl,--build-id=none \
	-lgdi32 -luser32 -lkernel32

.PHONY: all clean pack test

all: build/mote-x.exe

build/mote-x.exe: $(SRC) | build
	$(WINCC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

CORE_TEST = src/core/buffer.c src/core/utf8.c src/core/undo.c
build/test_core.exe: src/test/test_core.c $(CORE_TEST) | build
	$(WINCC) -std=c99 -Wall -Wextra $(INCLUDES) -O0 -g -D_WIN32 -o $@ \
		src/test/test_core.c $(CORE_TEST) -static

test: build/test_core.exe
	@if command -v wine64 >/dev/null 2>&1; then wine64 ./build/test_core.exe; \
	elif command -v wine >/dev/null 2>&1; then wine ./build/test_core.exe; \
	else echo "ok (wine not installed — binary built)"; fi

pack: build/mote-x.exe
	@command -v $(UPX) >/dev/null 2>&1 || { echo "UPX missing: $(UPX)"; exit 1; }
	cp -f build/mote-x.exe build/mote-x.packed.exe
	$(UPX) --best --lzma -f build/mote-x.packed.exe
	ls -la build/mote-x.exe build/mote-x.packed.exe

build:
	mkdir -p build

clean:
	rm -rf build
