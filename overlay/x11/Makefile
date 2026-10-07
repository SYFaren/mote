# mote — X11 overlay
CC ?= gcc
UPX_BIN ?= $(HOME)/.local/opt/upx/upx
ROOT := ../..
include ../build.mk

include ../core.mk
OV = main.c x11.c ../posix/os.c
SRC = $(CORE) $(OV)

INCLUDES = -I$(ROOT)/core -I$(ROOT)/plat
X11_CFLAGS := $(shell pkg-config --cflags x11 2>/dev/null)
X11_LIBS := $(shell pkg-config --libs x11 2>/dev/null)
ifeq ($(X11_LIBS),)
  X11_LIBS = -lX11
endif
CFLAGS = -std=c99 -Wall -Wextra $(INCLUDES) $(MOTE_POSIX) $(MOTE_OPT) $(MOTE_PLT) $(X11_CFLAGS)
LDFLAGS = $(X11_LIBS) $(MOTE_LD_OPT) $(MOTE_PLT)

.PHONY: all clean run test pack

all: $(MOTE_BUILDDIR)/mote

$(MOTE_BUILDDIR)/mote: $(SRC) $(CORE_H) | $(MOTE_BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

test:
	$(MAKE) -C $(ROOT) test

pack: $(MOTE_BUILDDIR)/mote
	@command -v $(UPX_BIN) >/dev/null 2>&1 || { echo "UPX missing: $(UPX_BIN)"; exit 1; }
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
