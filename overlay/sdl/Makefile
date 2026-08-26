# mote — SDL2 overlay (SDL3: make MOTE_SDL3=1)
CC ?= gcc
UPX_BIN ?= $(HOME)/.local/opt/upx/upx
ROOT := ../..
include ../build.mk

CORE = $(ROOT)/core/buffer.c $(ROOT)/core/utf8.c $(ROOT)/core/undo.c \
	$(ROOT)/core/hl.c $(ROOT)/core/editor.c $(ROOT)/core/theme.c \
	$(ROOT)/core/config.c $(ROOT)/core/mote_snprintf.c
OV = ../soft/main.c sdl.c ../posix/os.c ../soft/soft.c
SRC = $(CORE) $(OV)

INCLUDES = -I$(ROOT)/core -I$(ROOT)/plat -I../soft
CFLAGS = -std=c99 -Wall -Wextra $(INCLUDES) $(MOTE_POSIX) $(MOTE_OPT)
LDFLAGS = $(MOTE_LD_OPT)

ifeq ($(MOTE_SDL3),1)
  CFLAGS += -DMOTE_SDL3 $(shell pkg-config --cflags sdl3)
  LDFLAGS += $(shell pkg-config --libs sdl3)
else
  CFLAGS += $(shell pkg-config --cflags sdl2)
  LDFLAGS += $(shell pkg-config --libs sdl2)
endif

.PHONY: all clean run pack

all: build/mote

build/mote: $(SRC) $(ROOT)/plat/platform.h ../soft/soft.h | build
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
