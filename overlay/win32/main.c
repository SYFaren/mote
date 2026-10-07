/* mote — Win32 overlay entry */
#include "app.h"

static const MoteApp app = {
    "", "window size WxH in pixels (min 200x120)",
    "config: %APPDATA%\\mote\\config\n",
    "cannot open window", 200, 120, 0, 0, MOTE_FALSE, NULL};

int main(int argc, char **argv) { return mote_main(argc, argv, &app); }
