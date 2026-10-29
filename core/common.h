#ifndef MOTE_COMMON_H
#define MOTE_COMMON_H

#include "mote_ansi.h"

#define MOTE_NAME "mote"
#define MOTE_AUTHOR "SYFaren"
#define MOTE_VERSION "3.0.0"
#define MOTE_BUILD "2026-10-29"
#define MOTE_FONT_PX 15 /* retained platform setting; console terminals own fonts */
#define MOTE_FONT_MIN 8
#define MOTE_FONT_MAX 48
#define MOTE_RECENT 8 /* remembered recent files */
#define MOTE_MIN_WIN_W 200 /* generic pixel geometry bounds for future overlays */
#define MOTE_MIN_WIN_H 120
#define MOTE_MAX_FILE (64u * 1024u * 1024u)

/* ASCII-only case folding; UTF-8 bytes pass through unchanged. */
#define MOTE_LOWER(c) ((c) >= 'A' && (c) <= 'Z' ? (c) - 'A' + 'a' : (c))
#define MOTE_UPPER(c) ((c) >= 'a' && (c) <= 'z' ? (c) - 'a' + 'A' : (c))

#endif
