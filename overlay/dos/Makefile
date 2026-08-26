# mote — DOS overlay (DJGPP → FreeDOS / DOSBox)
#
# Toolchain: https://github.com/andrewwutw/build-djgpp/releases
#   export PATH="$HOME/.local/opt/djgpp/bin:$PATH"
# Then: make -C overlay/dos
#
DOSCC ?= i586-pc-msdosdjgpp-gcc
UPX_BIN ?= $(HOME)/.local/opt/upx/upx
ROOT := ../..

CORE = $(ROOT)/core/buffer.c $(ROOT)/core/utf8.c $(ROOT)/core/undo.c \
	$(ROOT)/core/hl.c $(ROOT)/core/editor.c $(ROOT)/core/theme.c \
	$(ROOT)/core/config.c $(ROOT)/core/mote_snprintf.c
OV = main.c dos.c os.c
SRC = $(CORE) $(OV)

INCLUDES = -I$(ROOT)/core -I$(ROOT)/plat
# Note: DJGPP always static-links libc + go32 stub (~70KB floor for empty main).
CFLAGS = -std=gnu99 -Wall -Os -DNDEBUG \
	-DMOTE_MAX_FILE=2097152u \
	$(INCLUDES) \
	-ffunction-sections -fdata-sections \
	-fno-exceptions -fno-asynchronous-unwind-tables -fno-unwind-tables \
	-fno-ident -fomit-frame-pointer
LDFLAGS = -Wl,--gc-sections -s

.PHONY: all clean pack

all: build/mote.exe

build/mote.exe: $(SRC) $(ROOT)/plat/platform.h | build
	@command -v $(DOSCC) >/dev/null 2>&1 || { \
	  echo "DOSCC=$(DOSCC) not found."; \
	  echo "Install DJGPP cross-gcc (andrewwutw/build-djgpp) and set PATH."; \
	  exit 1; \
	}
	$(DOSCC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)
	@ls -la $@

pack: build/mote.exe
	@command -v $(UPX_BIN) >/dev/null 2>&1 || { echo "UPX missing"; exit 1; }
	cp -f build/mote.exe build/mote.packed.exe
	env -u UPX $(UPX_BIN) --best --lzma -f build/mote.packed.exe
	ls -la build/mote.exe build/mote.packed.exe

build:
	mkdir -p build

clean:
	rm -rf build
