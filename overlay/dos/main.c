/* mote — DOS (DJGPP) overlay entry */
#include "app.h"

static const MoteApp app = {
    " (dos)", "text size COLSxROWS (40..132 x 10..50)",
    "DOS VGA text; Ctrl+Q quit, F1 help; config MOTE\\CONFIG\n"
    "env: MOTE_START_HELP=1  MOTE_KEYTRACE=1 (writes KEYTRACE.LOG)\n",
    "cannot init VGA text", 40, 10, 132, 50, MOTE_TRUE, NULL};

int main(int argc, char **argv) { return mote_main(argc, argv, &app); }
