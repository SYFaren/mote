# mote — Linux framebuffer (/dev/fb0)
CC ?= gcc
UPX_BIN ?= $(HOME)/.local/opt/upx/upx
ROOT := ../..
include ../build.mk

include ../core.mk
OV = ../soft/main.c fbdev.c ../posix/os.c ../soft/soft.c
SRC = $(CORE) $(OV)

INCLUDES = -I$(ROOT)/core -I$(ROOT)/plat -I../soft
CFLAGS = -std=c99 -Wall -Wextra -D_GNU_SOURCE $(INCLUDES) $(MOTE_OPT)
LDFLAGS = $(MOTE_LD_OPT)

.PHONY: all clean run pack

all: build/mote

build/mote: $(SRC) $(CORE_H) ../soft/soft.h | build
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

pack: build/mote
	@command -v $(UPX_BIN) >/dev/null 2>&1 || { echo "UPX missing"; exit 1; }
	cp -f build/mote build/mote.packed
	env -u UPX $(UPX_BIN) --best --lzma -f build/mote.packed
	ls -la build/mote build/mote.packed

run: build/mote
	./build/mote

build:
	mkdir -p build

clean:
	rm -rf build
