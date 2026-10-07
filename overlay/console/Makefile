# mote — console (TTY) overlay
CC ?= gcc
UPX_BIN ?= $(HOME)/.local/opt/upx/upx
ROOT := ../..
include ../build.mk

include ../core.mk
OV = main.c console.c ../posix/os.c
SRC = $(CORE) $(OV)

INCLUDES = -I$(ROOT)/core -I$(ROOT)/plat
CFLAGS = -std=c99 -Wall -Wextra $(INCLUDES) $(MOTE_POSIX) $(MOTE_OPT) $(MOTE_PLT)
LDFLAGS = $(MOTE_LD_OPT) $(MOTE_PLT)

.PHONY: all clean run pack

all: $(MOTE_BUILDDIR)/mote

$(MOTE_BUILDDIR)/mote: $(SRC) $(CORE_H) | $(MOTE_BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

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
