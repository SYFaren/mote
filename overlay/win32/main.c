/* mote — Win32 overlay entry */
#include "app.h"
#include "winutil.h"

static const MoteApp app = {
    "", "window size WxH in pixels (min 200x120)",
    "config: %APPDATA%\\mote\\config\n",
    "cannot open window", MOTE_MIN_WIN_W, MOTE_MIN_WIN_H, 0, 0, MOTE_FALSE, NULL};

/* wmain: the ANSI argv cannot hold every file name (e.g. Cyrillic on a
   Western locale); hand the core UTF-8 instead. */
int wmain(int argc, wchar_t **wargv) {
  char **argv = win_utf8_argv(argc, wargv);
  return argv ? mote_main(argc, argv, &app) : 1;
}
