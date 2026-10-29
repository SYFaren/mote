# Native POSIX terminal and MinGW Windows console builds share the C89 core.

CC ?= cc
WINCC ?= x86_64-w64-mingw32-gcc

.PHONY: all posix console linux windows winconsole test smoke ansi-check clean release

all: linux

posix console linux:
	$(MAKE) -C overlay/console CC="$(CC)" MOTE_BUILDDIR=build

windows winconsole:
	$(MAKE) -C overlay/winconsole WINCC="$(WINCC)" MOTE_BUILDDIR=build

ANSI_CFLAGS = -std=c89 -pedantic -Wall -Wextra -Wdeclaration-after-statement \
	-Wno-long-long -Wno-overlength-strings -Icore -Iplat -c
CORE_ANSI = core/buffer.c core/utf8.c core/undo.c core/hl.c core/editor.c \
	core/theme.c core/config.c core/mote_snprintf.c core/regex.c core/dirlist.c core/app.c \
	core/keymap.c

ansi-check: $(CORE_ANSI) plat/platform.h
	@mkdir -p build/ansi
	@for f in $(CORE_ANSI); do \
	  echo "ANSI $$f"; \
	  $(CC) $(ANSI_CFLAGS) -o build/ansi/$$(basename $$f .c).o $$f || exit 1; \
	done
	@echo "ANSI core OK"

test: build/test_core build/test_editor_keys build/test_console_esc
	./build/test_core
	./build/test_editor_keys
	@script -q -c "./build/test_console_esc" /dev/null 2>/dev/null || echo "skip test_console_esc (no TTY)"

CORE_TEST = core/buffer.c core/utf8.c core/undo.c core/mote_snprintf.c core/regex.c core/dirlist.c core/hl.c test/plat_stub.c
build/test_core: test/test_core.c $(CORE_TEST) plat/platform.h | build
	$(CC) -std=c89 -pedantic -Wall -Wextra -Wdeclaration-after-statement \
		-Wno-overlength-strings -Icore -Iplat -O0 -g -o $@ test/test_core.c $(CORE_TEST)

CORE_EDTEST = core/buffer.c core/utf8.c core/undo.c core/mote_snprintf.c core/regex.c core/dirlist.c core/hl.c \
	core/editor.c core/theme.c core/config.c core/keymap.c
build/test_editor_keys: test/test_editor_keys.c $(CORE_EDTEST) plat/platform.h | build
	$(CC) -std=c89 -Wall -Wextra -Wno-unused-parameter \
		-Icore -Iplat -O0 -g -o $@ test/test_editor_keys.c $(CORE_EDTEST)

build/test_console_esc: test/test_console_esc.c overlay/console/console.c core/utf8.c core/keymap.c plat/platform.h | build
	$(CC) -std=c99 -Wall -Wextra -D_DEFAULT_SOURCE -Icore -Iplat -DMOTE_TEST_CONSOLE_ESC -O0 -g \
		-o $@ test/test_console_esc.c overlay/console/console.c core/utf8.c core/keymap.c

smoke: ansi-check test
	@sh test/smoke.sh

release: test ansi-check
	$(MAKE) linux windows
	mkdir -p dist-release
	cp -f overlay/console/build/mote dist-release/mote-posix-console
	cp -f overlay/winconsole/build/mote.exe dist-release/mote-windows-console.exe
	cd dist-release && sha256sum mote-posix-console mote-windows-console.exe > SHA256SUMS

build:
	mkdir -p build

clean:
	rm -rf build
	$(MAKE) -C overlay/console clean MOTE_BUILDDIR=build
	$(MAKE) -C overlay/winconsole clean MOTE_BUILDDIR=build
