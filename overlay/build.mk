# Shared squeeze flags for mote overlays
# Tiny binary: LTO + GC sections, no unwind/ident/build-id, hidden syms.

_MOTE_UNAME := $(shell uname -s 2>/dev/null | tr '[:upper:]' '[:lower:]')
_MOTE_MACHINE := $(shell uname -m 2>/dev/null)

ifeq ($(MOTE_OS),)
  ifeq ($(_MOTE_UNAME),darwin)
    MOTE_OS := macos
  else ifneq ($(findstring linux,$(_MOTE_UNAME)),)
    MOTE_OS := linux
  else ifeq ($(_MOTE_UNAME),freebsd)
    MOTE_OS := freebsd
  else ifeq ($(_MOTE_UNAME),openbsd)
    MOTE_OS := openbsd
  else ifeq ($(_MOTE_UNAME),netbsd)
    MOTE_OS := netbsd
  else
    MOTE_OS := $(_MOTE_UNAME)
  endif
endif

ifeq ($(MOTE_ARCH),)
  ifeq ($(_MOTE_MACHINE),aarch64)
    MOTE_ARCH := arm64
  else ifeq ($(_MOTE_MACHINE),x86_64)
    MOTE_ARCH := amd64
  else ifeq ($(_MOTE_MACHINE),i386)
    MOTE_ARCH := i686
  else ifeq ($(_MOTE_MACHINE),armv7)
    MOTE_ARCH := armhf
  else
    MOTE_ARCH := amd64
  endif
endif

# Separate output dir for cross-builds so native build/ is not overwritten.
MOTE_BUILDDIR ?= build
ifeq ($(MOTE_OS),linux)
ifneq ($(filter-out amd64,$(MOTE_ARCH)),)
MOTE_BUILDDIR := build-$(MOTE_OS)-$(MOTE_ARCH)
endif
else
MOTE_BUILDDIR := build-$(MOTE_OS)-$(MOTE_ARCH)
endif

MOTE_OPT = -Oz -DNDEBUG \
	-ffunction-sections -fdata-sections \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident \
	-fomit-frame-pointer -fno-stack-protector \
	-fmerge-all-constants -flto -fno-math-errno \
	-fno-semantic-interposition -fvisibility=hidden \
	-fno-common

ifeq ($(MOTE_OS),macos)
MOTE_POSIX = -D_DARWIN_C_SOURCE
MOTE_PLT =
else ifeq ($(MOTE_OS),linux)
MOTE_POSIX = -D_DEFAULT_SOURCE
MOTE_PLT = -fno-plt
else
# BSD: SIGWINCH and other extensions (strict POSIX hides them).
MOTE_POSIX = -D_BSD_SOURCE
MOTE_PLT =
endif

ifeq ($(MOTE_OS),linux)
MOTE_LD_GNU = -Wl,-z,norelro,-z,noseparate-code
MOTE_LD_OPT = -Oz -flto \
	-Wl,--gc-sections,--as-needed,-s,--build-id=none \
	-Wl,--hash-style=gnu,-O1,--exclude-libs,ALL \
	$(MOTE_LD_GNU)
else ifeq ($(MOTE_OS),macos)
MOTE_LD_OPT = -Oz -flto -Wl,-dead_strip
else
MOTE_LD_OPT = -Oz -flto -Wl,--gc-sections -s
endif

ifeq ($(MOTE_STATIC),1)
ifneq ($(MOTE_OS),macos)
MOTE_LD_OPT += -static
endif
endif
