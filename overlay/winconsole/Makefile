WINCC ?= x86_64-w64-mingw32-gcc
ROOT := ../..
MOTE_BUILDDIR ?= build

include ../core.mk
OV = main.c winconsole.c os.c winutil.c
SRC = $(CORE) $(OV)

INCLUDES = -I$(ROOT)/core -I$(ROOT)/plat
CFLAGS = -std=c99 -Wall -Wextra $(INCLUDES) -Oz -DNDEBUG \
	-ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -fno-ident -fomit-frame-pointer -fno-stack-protector \
	-fmerge-all-constants -flto -fno-math-errno -fno-semantic-interposition \
	-fvisibility=hidden -fno-common -D_WIN32 -municode
LDFLAGS = -static -static-libgcc -Oz -flto \
	-Wl,--gc-sections,--as-needed,-s,--strip-all,--exclude-libs,ALL \
	-luser32 -lkernel32

.PHONY: all clean test

all: $(MOTE_BUILDDIR)/mote.exe

$(MOTE_BUILDDIR)/mote.exe: $(SRC) $(CORE_H) | $(MOTE_BUILDDIR)
	$(WINCC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

test:
	$(MAKE) -C $(ROOT) test

$(MOTE_BUILDDIR):
	mkdir -p $(MOTE_BUILDDIR)

clean:
	rm -rf $(MOTE_BUILDDIR)
