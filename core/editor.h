/* mote core — editor.h */
#ifndef MOTE_EDITOR_H
#define MOTE_EDITOR_H

#include "buffer.h"
#include "undo.h"
#include "platform.h"

#define MAX_DOCS 6
#define MAX_RECENT 8

typedef enum {
  MODE_EDIT = 0,
  MODE_OPEN,
  MODE_SAVEAS,
  MODE_FIND,
  MODE_REPLACE,
  MODE_GOTO,
  MODE_QUITASK,
  MODE_HELP,
  MODE_RECENT,
  MODE_QUICKOPEN,
  MODE_OPENASK,
  MODE_CLOSEASK
} EdMode;

#define MAX_BOOKMARKS 4
#define QF_MAX 64
#define QF_POOL 256

typedef enum { EOL_LF = 0, EOL_CRLF = 1 } EolMode;

typedef struct {
  size_t *off;
  size_t n, capa;
  mote_bool dirty;
} LineMap;

typedef struct {
  Buf buf;
  UndoStack undo;
  LineMap lines;
  char path[1024];
  mote_bool dirty, readonly;
  EolMode eol;
  size_t caret, sel_anchor, row0, col0, wrap0;
  size_t pref_col;
  size_t caret_row, caret_col;
  size_t row0_pos;
  mote_bool row0_valid;
  size_t match_a, match_b;
  size_t bracket_a, bracket_b;
  size_t bm_row[MAX_BOOKMARKS]; /* line index, (size_t)-1 = unset */
  int bm_jump;
  int hl_in_ml;
  size_t hl_ml_row;
  mote_bool hl_ml_valid;
} Doc;

typedef struct {
  Doc docs[MAX_DOCS];
  int ndocs, cur;
  mote_bool need_draw;
  mote_bool want_quit;
  mote_bool quit_after_save;
  mote_bool close_after_save;
  EdMode mode;
  char prompt[256];
  char status[96];
  char find[192];
  char replace[192];
  char pending_path[1024];
  mote_bool find_case, find_word, find_regex;
  mote_bool wrap, show_ws;
  char qf_dir[1024];
  char qf_pool[QF_POOL][256];
  int qf_pool_n;
  char qf_match[QF_MAX][256];
  int qf_n, qf_sel;
  mote_bool mouse_down;
  int cols, rows, cw, ch, gutter;
  int theme_id;
  char recent[MAX_RECENT][1024];
  int nrecent, recent_sel;
  size_t *vrow_cache;
  size_t vrow_n;
  int vrow_cols;
} Editor;

mote_bool ed_init(Editor *e);
void ed_free(Editor *e);
mote_bool ed_open_path(Editor *e, const char *path);
void ed_new_doc(Editor *e);
void ed_handle(Editor *e, Plat *p, const PlatEvent *ev);
void ed_draw(Editor *e, Plat *p);

#endif
