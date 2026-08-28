# Shared squeeze flags for mote overlays
# Tiny binary: LTO + GC sections, no unwind/ident/build-id, hidden syms.

MOTE_OS ?= $(shell uname -s 2>/dev/null | tr '[:upper:]' '[:lower:]' | sed 's/gnu\/linux/linux/')
MOTE_ARCH ?= amd64

# Separate output dir for cross-builds so native build/ is not overwritten.
MOTE_BUILDDIR ?= build
ifneq ($(filter-out amd64,$(MOTE_ARCH)),)
ifneq ($(MOTE_OS),)
MOTE_BUILDDIR := build-$(MOTE_OS)-$(MOTE_ARCH)
endif
endif

MOTE_OPT = -Oz -DNDEBUG \
	-ffunction-sections -fdata-sections \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident \
	-fomit-frame-pointer -fno-stack-protector \
	-fmerge-all-constants -flto -fno-math-errno \
	-fno-semantic-interposition -fvisibility=hidden \
	-fno-common

# putenv/setenv under -std=c99; harmless on MinGW/DJGPP when unused
MOTE_POSIX = -D_DEFAULT_SOURCE

# Linux ELF only — breaks BSD/macOS linkers
ifeq ($(MOTE_OS),linux)
MOTE_LD_GNU = -Wl,-z,norelro,-z,noseparate-code
else
MOTE_LD_GNU =
endif

MOTE_LD_OPT = -Oz -flto \
	-Wl,--gc-sections,--as-needed,-s,--build-id=none \
	-Wl,--hash-style=gnu,-O1,--exclude-libs,ALL \
	$(MOTE_LD_GNU)
