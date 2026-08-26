# mote — Wayland overlay
CC ?= gcc
UPX_BIN ?= $(HOME)/.local/opt/upx/upx
ROOT := ../..
include ../build.mk

CORE = $(ROOT)/core/buffer.c $(ROOT)/core/utf8.c $(ROOT)/core/undo.c \
	$(ROOT)/core/hl.c $(ROOT)/core/editor.c $(ROOT)/core/theme.c \
	$(ROOT)/core/config.c $(ROOT)/core/mote_snprintf.c
PROTO = gen/xdg-shell-protocol.c
OV = ../soft/main.c wayland.c ../posix/os.c ../soft/soft.c $(PROTO)
SRC = $(CORE) $(OV)

INCLUDES = -I$(ROOT)/core -I$(ROOT)/plat -I../soft -Igen
CFLAGS = -std=c99 -Wall -Wextra -D_GNU_SOURCE $(INCLUDES) $(MOTE_OPT) \
	$(shell pkg-config --cflags wayland-client xkbcommon)
LDFLAGS = $(shell pkg-config --libs wayland-client xkbcommon) $(MOTE_LD_OPT)

.PHONY: all clean run pack proto

all: proto build/mote

proto: gen/xdg-shell-protocol.c gen/xdg-shell-client-protocol.h

gen/xdg-shell-client-protocol.h: | gen
	wayland-scanner client-header \
	  /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml $@

gen/xdg-shell-protocol.c: | gen
	wayland-scanner private-code \
	  /usr/share/wayland-protocols/stable/xdg-shell/xdg-shell.xml $@

build/mote: $(SRC) $(ROOT)/plat/platform.h ../soft/soft.h | build
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

pack: build/mote
	@command -v $(UPX_BIN) >/dev/null 2>&1 || { echo "UPX missing"; exit 1; }
	cp -f build/mote build/mote.packed
	env -u UPX $(UPX_BIN) --best --lzma -f build/mote.packed
	ls -la build/mote build/mote.packed

run: build/mote
	./build/mote

gen build:
	mkdir -p $@

clean:
	rm -rf build
	# keep generated protocol in tree for offline builds
