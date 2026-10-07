/* mote — terminal (TTY) overlay entry */
#include "app.h"

static const MoteApp app = {
    " (console)", "text size COLSxROWS (40..512 x 10..256)",
    "truecolor TTY; Ctrl+Q quit, F1 help\nconfig: ~/.config/mote/config\n",
    "need a TTY (stdin/stdout)", 40, 10, 512, 256, MOTE_TRUE, NULL};

int main(int argc, char **argv) { return mote_main(argc, argv, &app); }
