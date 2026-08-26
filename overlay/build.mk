# Shared squeeze flags for mote overlays
# Tiny binary: LTO + GC sections, no unwind/ident/build-id, hidden syms.
MOTE_OPT = -Oz -DNDEBUG \
	-ffunction-sections -fdata-sections \
	-fno-asynchronous-unwind-tables -fno-unwind-tables -fno-ident \
	-fomit-frame-pointer -fno-stack-protector \
	-fmerge-all-constants -flto -fno-math-errno \
	-fno-semantic-interposition -fvisibility=hidden \
	-fno-common

# putenv/setenv under -std=c99 (glibc); harmless on MinGW/DJGPP when unused
MOTE_POSIX = -D_DEFAULT_SOURCE

MOTE_LD_OPT = -Oz -flto \
	-Wl,--gc-sections,--as-needed,-s,--build-id=none \
	-Wl,--hash-style=gnu,-O1,--exclude-libs,ALL
