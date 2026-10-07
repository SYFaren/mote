#ifndef MOTE_WINUTIL_H
#define MOTE_WINUTIL_H

#include "platform.h"
#include <wchar.h>

/* UTF-8 to UTF-16 and back, malloc'd, NULL on failure. */
wchar_t *win_wide(const char *utf8);
char *win_utf8(const wchar_t *w);
/* wmain's arguments as UTF-8 (the ANSI argv cannot hold every path). */
char **win_utf8_argv(int argc, wchar_t **wargv);
/* Key press to PlatKey; PK_NONE means not a shortcut (the key may type
   text). AltGr arrives as Ctrl+Alt and never triggers a shortcut. */
PlatKey win_vk_key(unsigned vk, mote_bool ctrl, mote_bool shift, mote_bool alt);

#endif
