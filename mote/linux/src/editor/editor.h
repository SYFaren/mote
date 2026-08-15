/* mote/mote/linux — developer: SYFaren */
#ifndef MOTE_EDITOR_H
#define MOTE_EDITOR_H
#include "buffer.h"
#include "undo.h"
#include "platform.h"
#include <stdbool.h>

typedef enum {
  MODE_EDIT = 0, MODE_OPEN, MODE_SAVEAS, MODE_FIND, MODE_GOTO,
  MODE_QUITASK, MODE_HELP, MODE_OPENASK
} EdMode;

typedef struct {
  size_t *off; /* byte start of each line */
  size_t n, capa;
  bool dirty;
} LineMap;

typedef struct {
  Buf buf;
  UndoStack undo;
  LineMap lines;
  char path[1024];
  bool dirty;
  bool need_draw;
  bool want_quit;
  size_t caret, sel_anchor, row0, col0;
  size_t pref_col;
  size_t caret_row, caret_col;
  size_t row0_pos;
  bool row0_valid;
  EdMode mode;
  char prompt[256];
  char status[96];
  char find[192];
  char pending_path[1024];
  bool mouse_down;
  int cols, rows, cw, ch;
} Editor;

bool ed_init(Editor *e);
void ed_free(Editor *e);
bool ed_open_path(Editor *e, const char *path);
void ed_handle(Editor *e, Plat *p, const PlatEvent *ev);
void ed_draw(Editor *e, Plat *p);

#endif
