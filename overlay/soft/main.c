/* mote — entry for the soft-framebuffer overlays (SDL, Wayland, fbdev, web) */
#include "app.h"
#ifdef __EMSCRIPTEN__
#include <emscripten.h>

/* The first paint can race the page's CSS layout: yield, then redraw. */
static void web_after_first_draw(Editor *e) {
  emscripten_sleep(0);
  e->need_draw = MOTE_TRUE;
}
#define AFTER_FIRST_DRAW web_after_first_draw
#else
#define AFTER_FIRST_DRAW NULL
#endif

static const MoteApp app = {
    "", "window size WxH in pixels (min 200x120)",
    "config: ~/.config/mote/config\n",
    "cannot open display", MOTE_MIN_WIN_W, MOTE_MIN_WIN_H, 0, 0, MOTE_FALSE, AFTER_FIRST_DRAW};

int main(int argc, char **argv) { return mote_main(argc, argv, &app); }
