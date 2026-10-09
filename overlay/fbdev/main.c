#include "app.h"

static const MoteApp app = {
    " (fbdev)", "area WxH in pixels (default: whole screen)",
    "run from a Linux text console (Ctrl+Alt+F3)\nMOTE_FB=/dev/fbN picks the framebuffer\n"
    "config: ~/.config/mote/config\n",
    "need a Linux text console and access to /dev/fb0 (video group)",
    MOTE_MIN_WIN_W, MOTE_MIN_WIN_H, 0, 0, MOTE_TRUE, NULL};

int main(int argc, char **argv) { return mote_main(argc, argv, &app); }
