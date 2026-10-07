/* mote — Windows console overlay entry */
#include "app.h"

static const MoteApp app = {
    " (winconsole)", "text size COLSxROWS (40..300 x 10..120)",
    "Windows console; Ctrl+Q quit, F1 help\nconfig: %APPDATA%\\mote\\config\n",
    "need a console", 40, 10, 300, 120, MOTE_TRUE, NULL};

int main(int argc, char **argv) { return mote_main(argc, argv, &app); }
