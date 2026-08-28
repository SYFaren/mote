# mote — SDL2 overlay (SDL3: make MOTE_SDL3=1)
CC ?= gcc
UPX_BIN ?= $(HOME)/.local/opt/upx/upx
ROOT := ../..
include ../build.mk

CORE = $(ROOT)/core/buffer.c $(ROOT)/core/utf8.c $(ROOT)/core/undo.c \
	$(ROOT)/core/hl.c $(ROOT)/core/editor.c $(ROOT)/core/theme.c \
	$(ROOT)/core/config.c $(ROOT)/core/mote_snprintf.c $(ROOT)/core/regex.c $(ROOT)/core/dirlist.c
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

all: $(MOTE_BUILDDIR)/mote

$(MOTE_BUILDDIR)/mote: $(SRC) $(ROOT)/plat/platform.h ../soft/soft.h | $(MOTE_BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

pack: $(MOTE_BUILDDIR)/mote
	@command -v $(UPX_BIN) >/dev/null 2>&1 || { echo "UPX missing"; exit 1; }
	cp -f $(MOTE_BUILDDIR)/mote $(MOTE_BUILDDIR)/mote.packed
	env -u UPX $(UPX_BIN) --best --lzma -f $(MOTE_BUILDDIR)/mote.packed
	ls -la $(MOTE_BUILDDIR)/mote $(MOTE_BUILDDIR)/mote.packed

run: $(MOTE_BUILDDIR)/mote
	./$(MOTE_BUILDDIR)/mote

$(MOTE_BUILDDIR):
	mkdir -p $(MOTE_BUILDDIR)

clean:
	rm -rf $(MOTE_BUILDDIR)

clean-all:
	rm -rf build build-*
