/* Shared Ctrl/Alt letter → PlatKey mapping for soft GUI overlays. */
#ifndef MOTE_SOFT_KEYS_H
#define MOTE_SOFT_KEYS_H

#include "platform.h"

static PlatKey soft_ctrl_letter(int ch, mote_bool shift, mote_bool alt) {
  int c = ch;
  if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
  if (alt && !shift) {
    /* Also used where Ctrl+Shift cannot be detected (DOS/TTY). */
    if (c == 'C') return PK_FINDCASE;
    if (c == 'W') return PK_FINDWORD;
    if (c == 'S') return PK_SAVEAS;
    if (c == 'R') return PK_READONLY;
    if (c == 'K') return PK_DELLINE;
    if (c == 'E') return PK_EOL;
    if (c == 'H') return PK_HELP;
    if (c == 'N') return PK_NEXTDOC;
    if (c == 'P') return PK_PREVDOC;
    if (c == 'M') return PK_BOOKMARK_SET;
    if (c == 'J') return PK_BOOKMARK;
    return PK_NONE;
  }
  switch (c) {
  case 'S': return shift ? PK_SAVEAS : PK_SAVE;
  case 'O': return PK_OPEN;
  case 'Q': return PK_QUIT;
  case 'Z': return PK_UNDO;
  case 'Y': return PK_REDO;
  case 'F': return PK_FIND;
  case 'X': return PK_CUT;
  case 'C': return PK_COPY;
  case 'V': return PK_PASTE;
  case 'A': return PK_SELALL;
  case 'H': return PK_HELP;
  case 'T': return PK_THEME;
  case 'G': return PK_GOTO;
  case 'R': return shift ? PK_READONLY : PK_REPLACE;
  case 'W': return shift ? PK_CLOSEDOC : PK_WRAP;
  case 'D': return PK_DUPLINE;
  case 'K': return shift ? PK_DELLINE : PK_NONE;
  case 'N': return PK_NEWDOC;
  case 'E': return shift ? PK_EOL : PK_RECENT;
  case ']': return PK_BRACKET;
  case '=':
  case '+': return PK_ZOOMIN;
  case '-':
  case '_': return PK_ZOOMOUT;
  case '0': return PK_ZOOMRESET;
  case 'M': return shift ? PK_BOOKMARK_SET : PK_BOOKMARK;
  case 'J': return PK_BOOKMARK;
  case 'P': return shift ? PK_BOOKMARK_SET : PK_QUICKOPEN;
  case '/':
  case '?': return PK_COMMENT;
  default: return PK_NONE;
  }
}

#endif
