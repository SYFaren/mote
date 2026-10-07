/* mote — Windows console overlay entry */
#include "app.h"
#include "winutil.h"

static const MoteApp app = {
    " (winconsole)", "text size COLSxROWS (40..300 x 10..120)",
    "Windows console; Ctrl+Q quit, F1 help\nconfig: %APPDATA%\\mote\\config\n",
    "need a console", 40, 10, 300, 120, MOTE_TRUE, NULL};

/* wmain: the ANSI argv cannot hold every file name (e.g. Cyrillic on a
   Western locale); hand the core UTF-8 instead. */
int wmain(int argc, wchar_t **wargv) {
  char **argv = win_utf8_argv(argc, wargv);
  return argv ? mote_main(argc, argv, &app) : 1;
}
