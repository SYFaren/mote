/* mote core — keymap.c */
#include "keymap.h"
#include "common.h"

PlatKey key_ctrl(int ch, mote_bool shift) {
  switch (MOTE_LOWER(ch)) {
  case 'a': return PK_SELALL;
  case 'b': return shift ? PK_BOOKMARK_SET : PK_BOOKMARK; /* TTYs: ^M is Enter */
  case 'c': return PK_COPY;
  case 'd': return PK_DUPLINE;
  case 'e': return shift ? PK_EOL : PK_RECENT;
  case 'f': return PK_FIND;
  case 'g': return PK_GOTO;
  case 'h': return PK_HELP;
  case 'j': return PK_BOOKMARK;
  case 'k': return shift ? PK_DELLINE : PK_NONE;
  case 'm': return shift ? PK_BOOKMARK_SET : PK_BOOKMARK;
  case 'n': return PK_NEWDOC;
  case 'o': return PK_OPEN;
  case 'p': return shift ? PK_BOOKMARK_SET : PK_QUICKOPEN;
  case 'q': return PK_QUIT;
  case 'r': return shift ? PK_READONLY : PK_REPLACE;
  case 's': return shift ? PK_SAVEAS : PK_SAVE;
  case 't': return PK_THEME;
  case 'v': return PK_PASTE;
  case 'w': return shift ? PK_CLOSEDOC : PK_WRAP;
  case 'x': return PK_CUT;
  case 'y': return PK_REDO;
  case 'z': return PK_UNDO;
  case '\t': return shift ? PK_PREVDOC : PK_NEXTDOC;
  case '\r': return shift ? PK_BOOKMARK_SET : PK_BOOKMARK;
  case '/':
  case '?': return PK_COMMENT;
  case ']':
  case '\\':
  case '|': return PK_BRACKET;
  case '=':
  case '+': return PK_ZOOMIN;
  case '-':
  case '_': return PK_ZOOMOUT;
  case '0': return PK_ZOOMRESET;
  default: return PK_NONE;
  }
}

PlatKey key_alt(int ch) {
  switch (MOTE_LOWER(ch)) {
  case 'c': return PK_FINDCASE;
  case 'e': return PK_EOL;
  case 'h': return PK_HELP;
  case 'j': return PK_BOOKMARK;
  case 'k': return PK_DELLINE;
  case 'm': return PK_BOOKMARK_SET;
  case 'n': return PK_NEXTDOC;
  case 'p': return PK_PREVDOC;
  case 'r': return PK_READONLY;
  case 's': return PK_SAVEAS;
  case 'w': return PK_FINDWORD;
  default: return PK_NONE;
  }
}

PlatKey key_fn(int n, mote_bool ctrl, mote_bool shift) {
  switch (n) {
  case 1: return PK_F1;
  case 2: return shift ? PK_PREVDOC : PK_NEXTDOC;
  case 3: return shift ? PK_FINDPREV : PK_FINDNEXT;
  case 4: return ctrl ? PK_CLOSEDOC : PK_NONE;
  case 5: return PK_RELOAD;
  case 7: return PK_WS;
  case 8: return PK_BOOKMARK_SET;
  case 9: return PK_BOOKMARK;
  default: return PK_NONE;
  }
}
