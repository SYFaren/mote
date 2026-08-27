# mote — root Makefile
.PHONY: all x11 win32 console winconsole dos sdl sdl3 wayland fbdev wasm \
	test smoke clean pack \
	dist release release-linux release-windows release-dos release-extra \
	release-zip ansi-check

all: x11

x11:
	$(MAKE) -C overlay/x11

win32:
	$(MAKE) -C overlay/win32

console:
	$(MAKE) -C overlay/console

winconsole:
	$(MAKE) -C overlay/winconsole

dos:
	$(MAKE) -C overlay/dos

sdl:
	$(MAKE) -C overlay/sdl

sdl3:
	@pkg-config --exists sdl3 || { echo "SDL3 not installed (pkg-config sdl3)"; exit 1; }
	$(MAKE) -C overlay/sdl MOTE_SDL3=1

wayland:
	$(MAKE) -C overlay/wayland

fbdev:
	$(MAKE) -C overlay/fbdev

wasm:
	@command -v emcc >/dev/null 2>&1 || { \
	  echo "emcc missing — source ~/.local/opt/emsdk/emsdk_env.sh"; exit 1; }
	$(MAKE) -C overlay/wasm

ANSI_CFLAGS = -std=c89 -pedantic -Wall -Wextra -Wdeclaration-after-statement \
	-Wno-long-long -Wno-overlength-strings -Icore -Iplat -c
CORE_ANSI = core/buffer.c core/utf8.c core/undo.c core/hl.c core/editor.c \
	core/theme.c core/config.c core/mote_snprintf.c

ansi-check: $(CORE_ANSI) plat/platform.h
	@mkdir -p build/ansi
	@for f in $(CORE_ANSI); do \
	  echo "ANSI $$f"; \
	  $(CC) $(ANSI_CFLAGS) -o build/ansi/$$(basename $$f .c).o $$f || exit 1; \
	done
	@echo "ANSI core OK"

test: build/test_core build/test_editor_keys
	./build/test_core
	./build/test_editor_keys

CORE_TEST = core/buffer.c core/utf8.c core/undo.c core/mote_snprintf.c core/hl.c test/plat_stub.c
build/test_core: test/test_core.c $(CORE_TEST) plat/platform.h | build
	$(CC) -std=c89 -pedantic -Wall -Wextra -Wdeclaration-after-statement \
		-Wno-overlength-strings -Icore -Iplat -O0 -g -o $@ test/test_core.c $(CORE_TEST)

CORE_EDTEST = core/buffer.c core/utf8.c core/undo.c core/mote_snprintf.c core/hl.c \
	core/editor.c core/theme.c core/config.c
build/test_editor_keys: test/test_editor_keys.c $(CORE_EDTEST) plat/platform.h | build
	$(CC) -std=c89 -Wall -Wextra -Wno-unused-parameter \
		-Icore -Iplat -O0 -g -o $@ test/test_editor_keys.c $(CORE_EDTEST)

smoke: ansi-check test
	@sh scripts/smoke.sh

pack: x11
	$(MAKE) -C overlay/x11 pack

DIST := dist-release
CAT := $(DIST)/by-platform

define COPY_BIN
	@mkdir -p $(1)
	@cp -f $(2) $(1)/$(3)
	@if [ -f $(4) ]; then cp -f $(4) $(1)/$(5); fi
endef

release-linux: console x11 sdl wayland fbdev
	@mkdir -p $(CAT)/linux/x11 $(CAT)/linux/console $(CAT)/linux/sdl2 \
	  $(CAT)/linux/wayland $(CAT)/linux/fbdev $(DIST)/flat
	@$(MAKE) -C overlay/x11 pack || true
	@$(MAKE) -C overlay/console pack || true
	@$(MAKE) -C overlay/sdl pack || true
	@$(MAKE) -C overlay/wayland pack || true
	@$(MAKE) -C overlay/fbdev pack || true
	cp -f overlay/x11/build/mote $(CAT)/linux/x11/mote
	cp -f overlay/console/build/mote $(CAT)/linux/console/mote
	cp -f overlay/sdl/build/mote $(CAT)/linux/sdl2/mote
	cp -f overlay/wayland/build/mote $(CAT)/linux/wayland/mote
	cp -f overlay/fbdev/build/mote $(CAT)/linux/fbdev/mote
	@for pair in \
	  "overlay/x11/build/mote.packed:$(CAT)/linux/x11/mote.upx:$(DIST)/flat/mote-linux-x11.upx" \
	  "overlay/console/build/mote.packed:$(CAT)/linux/console/mote.upx:$(DIST)/flat/mote-linux-console.upx" \
	  "overlay/sdl/build/mote.packed:$(CAT)/linux/sdl2/mote.upx:$(DIST)/flat/mote-linux-sdl2.upx" \
	  "overlay/wayland/build/mote.packed:$(CAT)/linux/wayland/mote.upx:$(DIST)/flat/mote-linux-wayland.upx" \
	  "overlay/fbdev/build/mote.packed:$(CAT)/linux/fbdev/mote.upx:$(DIST)/flat/mote-linux-fbdev.upx"; do \
	  src=$${pair%%:*}; rest=$${pair#*:}; dst=$${rest%%:*}; flat=$${rest#*:}; \
	  if [ -f "$$src" ]; then cp -f "$$src" "$$dst"; cp -f "$$src" "$$flat"; fi; \
	done
	cp -f overlay/x11/build/mote $(DIST)/flat/mote-linux-x11
	cp -f overlay/console/build/mote $(DIST)/flat/mote-linux-console
	cp -f overlay/sdl/build/mote $(DIST)/flat/mote-linux-sdl2
	cp -f overlay/wayland/build/mote $(DIST)/flat/mote-linux-wayland
	cp -f overlay/fbdev/build/mote $(DIST)/flat/mote-linux-fbdev
	@printf '%s\n' "X11 GUI" > $(CAT)/linux/x11/README.txt
	@printf '%s\n' "Unix TTY console" > $(CAT)/linux/console/README.txt
	@printf '%s\n' "SDL2 GUI (also builds as SDL3 when MOTE_SDL3=1)" > $(CAT)/linux/sdl2/README.txt
	@printf '%s\n' "Wayland (xdg-shell + wl_shm)" > $(CAT)/linux/wayland/README.txt
	@printf '%s\n' "Linux framebuffer /dev/fb0" > $(CAT)/linux/fbdev/README.txt

release-windows: win32 winconsole
	@mkdir -p $(CAT)/windows/gui $(CAT)/windows/console $(DIST)/flat
	@$(MAKE) -C overlay/win32 pack || true
	@$(MAKE) -C overlay/winconsole pack || true
	cp -f overlay/win32/build/mote.exe $(CAT)/windows/gui/mote.exe
	cp -f overlay/winconsole/build/mote.exe $(CAT)/windows/console/mote.exe
	cp -f overlay/win32/build/mote.exe $(DIST)/flat/mote-windows-gui.exe
	cp -f overlay/winconsole/build/mote.exe $(DIST)/flat/mote-windows-console.exe
	@if [ -f overlay/win32/build/mote.packed.exe ]; then \
	  cp -f overlay/win32/build/mote.packed.exe $(CAT)/windows/gui/mote.upx.exe; \
	  cp -f overlay/win32/build/mote.packed.exe $(DIST)/flat/mote-windows-gui.upx.exe; fi
	@if [ -f overlay/winconsole/build/mote.packed.exe ]; then \
	  cp -f overlay/winconsole/build/mote.packed.exe $(CAT)/windows/console/mote.upx.exe; \
	  cp -f overlay/winconsole/build/mote.packed.exe $(DIST)/flat/mote-windows-console.upx.exe; fi
	@printf '%s\n' "Windows GDI GUI" > $(CAT)/windows/gui/README.txt
	@printf '%s\n' "Windows console host" > $(CAT)/windows/console/README.txt

release-dos:
	@mkdir -p $(CAT)/dos $(DIST)/flat
	@$(MAKE) -C overlay/dos all
	@$(MAKE) -C overlay/dos pack || true
	cp -f overlay/dos/build/mote.exe $(CAT)/dos/mote.exe
	cp -f overlay/dos/build/mote.exe $(DIST)/flat/mote-dos.exe
	@if [ -f overlay/dos/runtime/CWSDPMI.EXE ]; then \
	  cp -f overlay/dos/runtime/CWSDPMI.EXE $(CAT)/dos/CWSDPMI.EXE; \
	  cp -f overlay/dos/runtime/README.txt $(CAT)/dos/CWSDPMI.txt; fi
	@if [ -f overlay/dos/build/mote.packed.exe ]; then \
	  cp -f overlay/dos/build/mote.packed.exe $(CAT)/dos/mote.upx.exe; \
	  cp -f overlay/dos/build/mote.packed.exe $(DIST)/flat/mote-dos.upx.exe; fi
	@printf '%s\n' "FreeDOS / DOSBox (DJGPP). Keep CWSDPMI.EXE next to mote.exe." > $(CAT)/dos/README.txt

release-extra:
	@mkdir -p $(CAT)/web/wasm $(DIST)/flat
	@if command -v emcc >/dev/null 2>&1; then \
	  $(MAKE) -C overlay/wasm; \
	  cp -f overlay/wasm/build/mote.html overlay/wasm/build/mote.js \
	        overlay/wasm/build/mote.wasm $(CAT)/web/wasm/ 2>/dev/null || true; \
	  cp -f overlay/wasm/build/mote.data $(CAT)/web/wasm/ 2>/dev/null || true; \
	  printf '%s\n' \
	    "mote WebAssembly (SDL2)" \
	    "" \
	    "Unpack mote-web.zip and open mote.html via a local HTTP server" \
	    "(file:// will not load .wasm / .data)." \
	    "" \
	    "  python3 -m http.server 8765" \
	    "  → http://127.0.0.1:8765/mote.html" \
	    > $(CAT)/web/wasm/README.txt; \
	  rm -f $(DIST)/flat/mote-web.zip; \
	  (cd $(CAT)/web/wasm && zip -q ../../../flat/mote-web.zip \
	    mote.html mote.js mote.wasm mote.data README.txt); \
	  ls -la $(DIST)/flat/mote-web.zip; \
	else echo "(skip wasm — no emcc)"; fi
	@if pkg-config --exists sdl3; then \
	  mkdir -p $(CAT)/linux/sdl3; \
	  $(MAKE) -C overlay/sdl clean; \
	  $(MAKE) -C overlay/sdl MOTE_SDL3=1; \
	  cp -f overlay/sdl/build/mote $(CAT)/linux/sdl3/mote; \
	  cp -f overlay/sdl/build/mote $(DIST)/flat/mote-linux-sdl3; \
	  printf '%s\n' "SDL3 GUI" > $(CAT)/linux/sdl3/README.txt; \
	  $(MAKE) -C overlay/sdl clean; \
	  $(MAKE) -C overlay/sdl; \
	else echo "(skip SDL3 — pkg-config sdl3 missing)"; fi

release-zip:
	@mkdir -p $(DIST)
	@rm -f $(DIST)/mote-all-platforms.zip
	@printf '%s\n' \
	  "mote multi-platform release" \
	  "" \
	  "by-platform/" \
	  "  linux/{x11,wayland,sdl2,sdl3?,console,fbdev}/" \
	  "  windows/{gui,console}/" \
	  "  dos/" \
	  "  web/wasm/" \
	  "flat/   — same binaries with stable GitHub asset names" \
	  "" \
	  "See README in each folder." > $(CAT)/README.txt
	@cd $(DIST) && zip -r -q mote-all-platforms.zip by-platform flat SHA256SUMS 2>/dev/null || \
	  (cd $(DIST) && zip -r -q mote-all-platforms.zip by-platform)
	@ls -la $(DIST)/mote-all-platforms.zip

# Build everything this host can; write checksums + zip
release:
	@rm -rf $(DIST)/by-platform $(DIST)/flat $(DIST)/mote-all-platforms.zip
	@rm -f $(DIST)/mote-* $(DIST)/SHA256SUMS
	@mkdir -p $(DIST)
	@$(MAKE) release-linux release-windows
	@if command -v i586-pc-msdosdjgpp-gcc >/dev/null 2>&1; then \
	  $(MAKE) release-dos; \
	else echo "(skip DOS — no DJGPP on PATH)"; fi
	@$(MAKE) release-extra || true
	@cd $(DIST)/flat && (command -v sha256sum >/dev/null && sha256sum * > ../SHA256SUMS || \
	  shasum -a 256 * > ../SHA256SUMS)
	@cp -f $(DIST)/SHA256SUMS $(DIST)/flat/SHA256SUMS
	@$(MAKE) release-zip
	@echo "=== $(DIST) ===" && find $(DIST) -type f | sort

dist: release

build:
	mkdir -p build

clean:
	rm -rf build
	$(MAKE) -C overlay/x11 clean
	$(MAKE) -C overlay/win32 clean
	$(MAKE) -C overlay/console clean
	$(MAKE) -C overlay/winconsole clean
	$(MAKE) -C overlay/dos clean
	$(MAKE) -C overlay/sdl clean
	$(MAKE) -C overlay/wayland clean
	$(MAKE) -C overlay/fbdev clean
	$(MAKE) -C overlay/wasm clean
