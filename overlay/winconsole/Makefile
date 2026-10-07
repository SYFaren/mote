# mote — Windows console overlay (MinGW, -mconsole)
WINCC ?= x86_64-w64-mingw32-gcc
UPX_BIN ?= $(HOME)/.local/opt/upx/upx
ROOT := ../..
include ../build.mk

include ../core.mk
OV = main.c winconsole.c ../win32/os.c ../win32/winutil.c
SRC = $(CORE) $(OV)

INCLUDES = -I$(ROOT)/core -I$(ROOT)/plat -I$(ROOT)/overlay/win32
CFLAGS = -std=c99 -Wall -Wextra $(INCLUDES) $(MOTE_OPT) -D_WIN32 -municode
LDFLAGS = -static -static-libgcc -Oz -flto \
	-Wl,--gc-sections,--as-needed,-s,--strip-all,--exclude-libs,ALL \
	-luser32 -lkernel32

.PHONY: all clean pack

all: build/mote.exe

build/mote.exe: $(SRC) $(CORE_H) | build
	$(WINCC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

pack: build/mote.exe
	@command -v $(UPX_BIN) >/dev/null 2>&1 || { echo "UPX missing: $(UPX_BIN)"; exit 1; }
	cp -f build/mote.exe build/mote.packed.exe
	env -u UPX $(UPX_BIN) --best --lzma -f build/mote.packed.exe
	ls -la build/mote.exe build/mote.packed.exe

build:
	mkdir -p build

clean:
	rm -rf build
