CC ?= gcc
ROOT := ../..
MOTE_BUILDDIR ?= build

include ../core.mk
OV = main.c console.c os.c
SRC = $(CORE) $(OV)

INCLUDES = -I$(ROOT)/core -I$(ROOT)/core/app -I$(ROOT)/core/editor \
	-I$(ROOT)/core/document -I$(ROOT)/core/syntax -I$(ROOT)/core/utils \
	-I$(ROOT)/core/system -I$(ROOT)/plat
CFLAGS = -std=c99 -Wall -Wextra $(INCLUDES) -D_DEFAULT_SOURCE -Oz -DNDEBUG \
	-ffunction-sections -fdata-sections -fno-asynchronous-unwind-tables \
	-fno-unwind-tables -fno-ident -fomit-frame-pointer -fno-stack-protector \
	-fmerge-all-constants -flto -fno-math-errno -fno-semantic-interposition \
	-fvisibility=hidden -fno-common -fno-plt
LDFLAGS = -Oz -flto -Wl,--gc-sections,--as-needed,-s,--build-id=none \
	-Wl,--hash-style=gnu,-O1,--exclude-libs,ALL -Wl,-z,norelro,-z,noseparate-code -fno-plt

.PHONY: all clean run

all: $(MOTE_BUILDDIR)/mote

$(MOTE_BUILDDIR)/mote: $(SRC) $(CORE_H) | $(MOTE_BUILDDIR)
	$(CC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

run: $(MOTE_BUILDDIR)/mote
	./$(MOTE_BUILDDIR)/mote

$(MOTE_BUILDDIR):
	mkdir -p $(MOTE_BUILDDIR)

clean:
	rm -rf $(MOTE_BUILDDIR)
