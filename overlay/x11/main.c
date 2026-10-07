#include "app.h"

static const MoteApp app = {
    "", "window size WxH in pixels (min 200x120)",
    "config: ~/.config/mote/config\n",
    "cannot open window", MOTE_MIN_WIN_W, MOTE_MIN_WIN_H, 0, 0, MOTE_FALSE, NULL};

int main(int argc, char **argv) { return mote_main(argc, argv, &app); }
