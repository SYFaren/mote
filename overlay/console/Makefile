# mote — console (TTY) overlay
CC ?= gcc
UPX_BIN ?= $(HOME)/.local/opt/upx/upx
ROOT := ../..
include ../build.mk

CORE = $(ROOT)/core/buffer.c $(ROOT)/core/utf8.c $(ROOT)/core/undo.c \
	$(ROOT)/core/hl.c $(ROOT)/core/editor.c $(ROOT)/core/theme.c \
	$(ROOT)/core/config.c $(ROOT)/core/mote_snprintf.c $(ROOT)/core/regex.c $(ROOT)/core/dirlist.c
OV = main.c console.c ../posix/os.c
SRC = $(CORE) $(OV)

INCLUDES = -I$(ROOT)/core -I$(ROOT)/plat
CFLAGS = -std=c99 -Wall -Wextra $(INCLUDES) $(MOTE_POSIX) $(MOTE_OPT) -fno-plt
LDFLAGS = $(MOTE_LD_OPT) -Wl,-z,norelro,-z,noseparate-code -fno-plt

.PHONY: all clean run pack

all: build/mote

build/mote: $(SRC) $(ROOT)/plat/platform.h | build
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

pack: build/mote
	@command -v $(UPX_BIN) >/dev/null 2>&1 || { echo "UPX missing: $(UPX_BIN)"; exit 1; }
	cp -f build/mote build/mote.packed
	env -u UPX $(UPX_BIN) --best --lzma -f build/mote.packed
	ls -la build/mote build/mote.packed

run: build/mote
	./build/mote

build:
	mkdir -p build

clean:
	rm -rf build
