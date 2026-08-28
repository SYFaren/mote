# mote — WebAssembly (Emscripten + SDL2)
# Needs: source ~/.local/opt/emsdk/emsdk_env.sh
EMCC ?= emcc
ROOT := ../..

CORE = $(ROOT)/core/buffer.c $(ROOT)/core/utf8.c $(ROOT)/core/undo.c \
	$(ROOT)/core/hl.c $(ROOT)/core/editor.c $(ROOT)/core/theme.c \
	$(ROOT)/core/config.c $(ROOT)/core/mote_snprintf.c $(ROOT)/core/regex.c $(ROOT)/core/dirlist.c
OV = ../soft/main.c ../sdl/sdl.c ../posix/os.c ../soft/soft.c
SRC = $(CORE) $(OV)

INCLUDES = -I$(ROOT)/core -I$(ROOT)/plat -I../soft
CFLAGS = -std=gnu99 -Wall -O2 -DNDEBUG $(INCLUDES)
LDFLAGS = -sUSE_SDL=2 -sALLOW_MEMORY_GROWTH=1 -sASYNCIFY \
	-sFORCE_FILESYSTEM=1 --shell-file shell.html \
	--preload-file ../../examples/hello_mote.c@hello_mote.c \
	-sEXPORTED_RUNTIME_METHODS=['ccall']

.PHONY: all clean serve

all: build/mote.html

build/mote.html: $(SRC) shell.html $(ROOT)/plat/platform.h | build
	$(EMCC) $(CFLAGS) -o $@ $(SRC) $(LDFLAGS)

serve: build/mote.html
	cd build && $(EMSDK_NODE) -e "require('http').createServer((q,s)=>{require('fs').readFile('.'+q.url.split('?')[0].replace(/\/$$/,'/mote.html'),(e,d)=>{s.end(d||'')})}).listen(8765)" || python3 -m http.server 8765

build:
	mkdir -p build

clean:
	rm -rf build
