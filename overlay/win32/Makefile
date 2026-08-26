# mote — Win32 overlay (static MinGW cross-build)
WINCC ?= x86_64-w64-mingw32-gcc
UPX ?= $(HOME)/.local/opt/upx/upx
ROOT := ../..
include ../build.mk

CORE = $(ROOT)/core/buffer.c $(ROOT)/core/utf8.c $(ROOT)/core/undo.c \
	$(ROOT)/core/hl.c $(ROOT)/core/editor.c $(ROOT)/core/theme.c \
	$(ROOT)/core/config.c $(ROOT)/core/mote_snprintf.c
OV = main.c win32.c os.c
SRC = $(CORE) $(OV)

INCLUDES = -I$(ROOT)/core -I$(ROOT)/plat
CFLAGS = -std=c99 -Wall -Wextra $(INCLUDES) $(MOTE_OPT) -D_WIN32 -mwindows
# PE/MinGW: no ELF -z flags; strip + GC + LTO only
LDFLAGS = -static -static-libgcc -Oz -flto \
	-Wl,--gc-sections,--as-needed,-s,--strip-all,--exclude-libs,ALL \
	-lgdi32 -luser32 -lkernel32

.PHONY: all clean pack test

all: build/mote.exe

build/mote.exe: $(SRC) $(ROOT)/plat/platform.h | build
	$(WINCC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

test:
	$(MAKE) -C $(ROOT) test

pack: build/mote.exe
	@command -v $(UPX) >/dev/null 2>&1 || { echo "UPX missing: $(UPX)"; exit 1; }
	cp -f build/mote.exe build/mote.packed.exe
	$(UPX) --best --lzma -f build/mote.packed.exe
	ls -la build/mote.exe build/mote.packed.exe

build:
	mkdir -p build

clean:
	rm -rf build
