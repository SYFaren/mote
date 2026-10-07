#ifndef MOTE_COMMON_H
#define MOTE_COMMON_H

#include "mote_ansi.h"

#define MOTE_NAME "mote"
#define MOTE_AUTHOR "SYFaren"
#define MOTE_VERSION "2.1.1"
#define MOTE_BUILD "2026-10-07"
#define MOTE_FONT_PX 15 /* default font size, Ctrl+0 resets to it */
#define MOTE_FONT_MIN 8
#define MOTE_FONT_MAX 48
#define MOTE_RECENT 8 /* remembered recent files */
#define MOTE_MIN_WIN_W 200 /* smallest GUI window, pixels */
#define MOTE_MIN_WIN_H 120
#ifndef MOTE_MAX_FILE
#define MOTE_MAX_FILE (64u * 1024u * 1024u)
#endif

/* ASCII-only case folding; UTF-8 bytes pass through unchanged. */
#define MOTE_LOWER(c) ((c) >= 'A' && (c) <= 'Z' ? (c) - 'A' + 'a' : (c))
#define MOTE_UPPER(c) ((c) >= 'a' && (c) <= 'z' ? (c) - 'a' + 'A' : (c))

#endif
