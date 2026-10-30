#ifndef MOTE_KEYMAP_H
#define MOTE_KEYMAP_H

#include "platform.h"

/* Ctrl+<ch>. ch is the ASCII the key produces (either case); '\t' and '\r'
   stand for Ctrl+Tab and Ctrl+Enter. PK_NONE if unbound. */
PlatKey key_ctrl(int ch, mote_bool shift);
/* Alt+<letter>, either case. PK_NONE if unbound (the key then types text). */
PlatKey key_alt(int ch);
/* F1 to F12. */
PlatKey key_fn(int n, mote_bool ctrl, mote_bool shift);

#endif
