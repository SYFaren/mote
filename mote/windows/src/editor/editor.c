/* mote/mote/windows — developer: SYFaren */
#include "editor.h"
#include "utf8.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>

#define COL_BG 0x1E1E1E
#define COL_FG 0xD4D4D4
#define COL_SEL 0x264F78
#define COL_STAT 0x0E639C
#define COL_STATFG 0xFFFFFF
#define COL_HELPBG 0x2D2D30
#define COL_HELPBD 0x007ACC

static void mark(Editor *e) { e->need_draw = true; }
static void view_invalid(Editor *e) { e->row0_valid = false; }

static void lines_mark_dirty(Editor *e) {
  e->lines.dirty = true;
  e->row0_valid = false;
}

/* Shift line starts after byte pos by delta (no newline in edit). */
static void lines_shift(Editor *e, size_t pos, long delta) {
  size_t i;
  if (e->lines.dirty || !e->lines.off || !delta) return;
  for (i = 0; i < e->lines.n; i++) {
    if (e->lines.off[i] > pos)
      e->lines.off[i] = (size_t)((long)e->lines.off[i] + delta);
  }
  e->row0_valid = false;
}

static bool lines_push(LineMap *m, size_t off) {
  size_t *no;
  if (m->n == m->capa) {
    size_t nc = m->capa ? m->capa * 2 : 64;
    no = (size_t *)realloc(m->off, nc * sizeof(size_t));
    if (!no) return false;
    m->off = no;
    m->capa = nc;
  }
  m->off[m->n++] = off;
  return true;
}

static void lines_rebuild(Editor *e) {
  size_t i, len = buf_len(&e->buf);
  e->lines.n = 0;
  if (!lines_push(&e->lines, 0)) return;
  for (i = 0; i < len; i++) {
    if (buf_at(&e->buf, i) == '\n') {
      if (!lines_push(&e->lines, i + 1)) return;
    }
  }
  e->lines.dirty = false;
}

static void ensure_lines(Editor *e) {
  if (e->lines.dirty || !e->lines.off) lines_rebuild(e);
}

static void set_status(Editor *e, const char *s) {
  snprintf(e->status, sizeof e->status, "%s", s ? s : "");
  mark(e);
}
static size_t sel_lo(const Editor *e) {
  return e->caret < e->sel_anchor ? e->caret : e->sel_anchor;
}
static size_t sel_hi(const Editor *e) {
  return e->caret > e->sel_anchor ? e->caret : e->sel_anchor;
}
static bool has_sel(const Editor *e) { return e->caret != e->sel_anchor; }
static void clear_sel(Editor *e) { e->sel_anchor = e->caret; }

static char *slice_dup(const Editor *e, size_t a, size_t b) {
  size_t n = b - a;
  char *s = (char *)malloc(n + 1);
  if (!s) return NULL;
  buf_get(&e->buf, a, n, s);
  s[n] = 0;
  return s;
}

/* Display columns: 1/codepoint, tabs → 4. */
static int tab_cols(size_t col) {
  int w = 4 - (int)(col % 4);
  return w <= 0 ? 4 : w;
}

/* Advance i..stop updating *col. If stop_want, return when col would pass want. */
static size_t disp_advance(Editor *e, size_t i, size_t stop, size_t *col,
                           size_t want, int stop_want) {
  size_t len = buf_len(&e->buf);
  if (stop > len) stop = len;
  while (i < stop) {
    char chunk[4];
    uint32_t cp;
    int n, k, w;
    size_t rem = len - i;
    for (k = 0; k < 4 && (size_t)k < rem; k++)
      chunk[k] = buf_at(&e->buf, i + (size_t)k);
    n = utf8_decode(chunk, rem < 4 ? rem : 4, &cp);
    if (n <= 0) {
      n = 1;
      cp = '?';
    }
    if (cp == '\n') break;
    w = (cp == '\t') ? tab_cols(*col) : 1;
    if (stop_want && *col + (size_t)w > want) return i;
    *col += (size_t)w;
    i += (size_t)n;
  }
  return i;
}

static size_t disp_col_between(Editor *e, size_t start, size_t pos) {
  size_t col = 0;
  disp_advance(e, start, pos, &col, 0, 0);
  return col;
}

static size_t pos_at_disp_col(Editor *e, size_t start, size_t end,
                              size_t want) {
  size_t col = 0;
  return disp_advance(e, start, end, &col, want, 1);
}

/* Binary search line starts — O(log lines); col is display column. */
static void pos_to_rc(Editor *e, size_t pos, size_t *row, size_t *col) {
  size_t lo, hi, len = buf_len(&e->buf);
  ensure_lines(e);
  if (pos > len) pos = len;
  if (!e->lines.n) {
    *row = 0;
    *col = disp_col_between(e, 0, pos);
    return;
  }
  lo = 0;
  hi = e->lines.n;
  while (lo + 1 < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (e->lines.off[mid] <= pos) lo = mid;
    else hi = mid;
  }
  *row = lo;
  *col = disp_col_between(e, e->lines.off[lo], pos);
}

static size_t rc_to_pos(Editor *e, size_t row, size_t col) {
  size_t start, end, len = buf_len(&e->buf);
  ensure_lines(e);
  if (!e->lines.n) return 0;
  if (row >= e->lines.n) return len;
  start = e->lines.off[row];
  if (row + 1 < e->lines.n)
    end = e->lines.off[row + 1] > 0 ? e->lines.off[row + 1] - 1 : 0;
  else
    end = len;
  return pos_at_disp_col(e, start, end, col);
}

static size_t line_start(const Editor *e, size_t pos) {
  while (pos > 0 && buf_at(&e->buf, pos - 1) != '\n') pos--;
  return pos;
}
static size_t line_end(const Editor *e, size_t pos) {
  size_t len = buf_len(&e->buf);
  while (pos < len && buf_at(&e->buf, pos) != '\n') pos++;
  return pos;
}

static void sync_caret_rc(Editor *e) {
  pos_to_rc(e, e->caret, &e->caret_row, &e->caret_col);
  e->pref_col = e->caret_col;
}

static void ensure_visible(Editor *e) {
  size_t row = e->caret_row, col = e->caret_col, old = e->row0;
  if (row < e->row0) e->row0 = row;
  if (e->rows > 0 && row >= e->row0 + (size_t)e->rows)
    e->row0 = row - (size_t)e->rows + 1;
  if (col < e->col0) e->col0 = col;
  if (e->cols > 0 && col >= e->col0 + (size_t)e->cols)
    e->col0 = col - (size_t)e->cols + 1;
  if (e->row0 != old) view_invalid(e);
}

static void clamp_caret(Editor *e) {
  size_t len = buf_len(&e->buf);
  if (e->caret > len) e->caret = len;
  if (e->sel_anchor > len) e->sel_anchor = len;
}

static void push_delete(Editor *e, size_t pos, size_t n) {
  char *t;
  size_t i;
  bool has_nl = false;
  if (!n) return;
  t = (char *)malloc(n);
  if (!t) return;
  buf_get(&e->buf, pos, n, t);
  for (i = 0; i < n; i++) {
    if (t[i] == '\n') {
      has_nl = true;
      break;
    }
  }
  if (!undo_push(&e->undo, U_DELETE, pos, t, n, false)) {
    free(t);
    set_status(e, "out of memory");
    return;
  }
  free(t);
  buf_delete(&e->buf, pos, n);
  e->dirty = true;
  clamp_caret(e);
  if (has_nl) lines_mark_dirty(e);
  else lines_shift(e, pos, -(long)n);
  mark(e);
}

static void push_insert(Editor *e, size_t pos, const char *s, size_t n,
                        bool coalesce) {
  size_t i;
  bool has_nl = false;
  if (!n || !s) return;
  for (i = 0; i < n; i++) {
    if (s[i] == '\n') {
      has_nl = true;
      break;
    }
  }
  if (!buf_insert(&e->buf, pos, s, n)) {
    set_status(e, "out of memory");
    return;
  }
  if (!undo_push(&e->undo, U_INSERT, pos, s, n, coalesce)) {
    buf_delete(&e->buf, pos, n);
    set_status(e, "out of memory");
    return;
  }
  e->dirty = true;
  clamp_caret(e);
  if (has_nl) lines_mark_dirty(e);
  else lines_shift(e, pos, (long)n);
  mark(e);
}

static void delete_sel(Editor *e) {
  size_t a, b;
  if (!has_sel(e)) return;
  a = sel_lo(e);
  b = sel_hi(e);
  push_delete(e, a, b - a);
  e->caret = a;
  clear_sel(e);
  sync_caret_rc(e);
}

static void insert_text(Editor *e, const char *s, size_t n) {
  bool coal = (n <= 4 && n > 0 && s[0] != '\n' && s[0] != '\t');
  delete_sel(e);
  push_insert(e, e->caret, s, n, coal);
  e->caret += n;
  clear_sel(e);
  sync_caret_rc(e);
  ensure_visible(e);
}

static void do_undo(Editor *e) {
  UndoAct *a = undo_pop_undo(&e->undo);
  if (!a) return;
  if (a->kind == U_INSERT) {
    buf_delete(&e->buf, a->pos, a->len);
    e->caret = a->pos;
  } else {
    if (!buf_insert(&e->buf, a->pos, a->text, a->len)) {
      e->undo.head++; /* restore after failed apply */
      set_status(e, "undo failed");
      return;
    }
    e->caret = a->pos + a->len;
  }
  clear_sel(e);
  clamp_caret(e);
  sync_caret_rc(e);
  e->dirty = true;
  ensure_visible(e);
  lines_mark_dirty(e);
  mark(e);
}

static void do_redo(Editor *e) {
  UndoAct *a = undo_pop_redo(&e->undo);
  if (!a) return;
  if (a->kind == U_INSERT) {
    if (!buf_insert(&e->buf, a->pos, a->text, a->len)) {
      e->undo.head--; /* restore after failed apply */
      set_status(e, "redo failed");
      return;
    }
    e->caret = a->pos + a->len;
  } else {
    buf_delete(&e->buf, a->pos, a->len);
    e->caret = a->pos;
  }
  clear_sel(e);
  clamp_caret(e);
  sync_caret_rc(e);
  e->dirty = true;
  ensure_visible(e);
  lines_mark_dirty(e);
  mark(e);
}

static void move_caret(Editor *e, size_t np, bool keep_sel) {
  e->caret = np;
  clamp_caret(e);
  buf_seek(&e->buf, e->caret);
  if (!keep_sel) clear_sel(e);
  sync_caret_rc(e);
  ensure_visible(e);
  mark(e);
}

static size_t ed_prev(const Editor *e, size_t i) {
  if (!i) return 0;
  i--;
  while (i && ((unsigned char)buf_at(&e->buf, i) & 0xC0) == 0x80) i--;
  return i;
}

static size_t ed_next(const Editor *e, size_t i) {
  char t[4];
  uint32_t cp;
  size_t len = buf_len(&e->buf);
  int got, n;
  if (i >= len) return len;
  got = (int)(len - i);
  if (got > 4) got = 4;
  buf_get(&e->buf, i, (size_t)got, t);
  n = utf8_decode(t, (size_t)got, &cp);
  return i + (size_t)(n > 0 ? n : 1);
}

static int is_word(unsigned char c) {
  return isalnum(c) || c == '_';
}

static size_t next_word(const Editor *e, size_t p) {
  size_t len = buf_len(&e->buf);
  while (p < len && !is_word((unsigned char)buf_at(&e->buf, p))) p = ed_next(e, p);
  while (p < len && is_word((unsigned char)buf_at(&e->buf, p))) p = ed_next(e, p);
  return p;
}

static size_t prev_word(const Editor *e, size_t p) {
  if (!p) return 0;
  p = ed_prev(e, p);
  while (p > 0 && !is_word((unsigned char)buf_at(&e->buf, p))) p = ed_prev(e, p);
  while (p > 0) {
    size_t q = ed_prev(e, p);
    if (!is_word((unsigned char)buf_at(&e->buf, q))) break;
    p = q;
  }
  return p;
}

static void move_vert(Editor *e, int dy, bool keep_sel) {
  size_t np;
  if (dy < 0 && e->caret_row == 0) {
    e->caret = 0;
    e->caret_row = e->caret_col = 0;
    if (!keep_sel) clear_sel(e);
    ensure_visible(e);
    mark(e);
    return;
  }
  e->caret_row = (size_t)((int)e->caret_row + dy);
  np = rc_to_pos(e, e->caret_row, e->pref_col);
  e->caret = np;
  clamp_caret(e);
  pos_to_rc(e, e->caret, &e->caret_row, &e->caret_col);
  if (!keep_sel) clear_sel(e);
  ensure_visible(e);
  mark(e);
}

static bool save_to(Editor *e, const char *path) {
  if (!path || !path[0]) {
    set_status(e, "empty path");
    return false;
  }
  if (!buf_save(&e->buf, path)) {
    set_status(e, "save failed");
    return false;
  }
  snprintf(e->path, sizeof e->path, "%s", path);
  e->dirty = false;
  set_status(e, "saved");
  return true;
}

static void try_save(Editor *e) {
  if (e->path[0]) save_to(e, e->path);
  else {
    e->mode = MODE_SAVEAS;
    e->prompt[0] = 0;
    set_status(e, "Save As — path");
  }
}

static void find_next(Editor *e) {
  size_t len, i, flen;
  if (!e->find[0]) return;
  len = buf_len(&e->buf);
  flen = strlen(e->find);
  if (!flen || flen > len) {
    set_status(e, "not found");
    return;
  }
  for (i = e->caret + (e->caret < len ? 1 : 0); i + flen <= len; i++) {
    if (buf_match(&e->buf, i, e->find, flen)) {
      e->sel_anchor = i;
      e->caret = i + flen;
      sync_caret_rc(e);
      ensure_visible(e);
      set_status(e, "found");
      return;
    }
  }
  for (i = 0; i + flen <= len && i <= e->caret; i++) {
    if (buf_match(&e->buf, i, e->find, flen)) {
      e->sel_anchor = i;
      e->caret = i + flen;
      sync_caret_rc(e);
      ensure_visible(e);
      set_status(e, "found (wrap)");
      return;
    }
  }
  set_status(e, "not found");
}

static void find_prev(Editor *e) {
  size_t len, i, flen, start;
  if (!e->find[0]) return;
  len = buf_len(&e->buf);
  flen = strlen(e->find);
  if (!flen || flen > len) {
    set_status(e, "not found");
    return;
  }
  start = e->caret > 0 ? e->caret - 1 : 0;
  if (start + 1 >= flen) {
    i = start + 1 - flen;
    for (;;) {
      if (buf_match(&e->buf, i, e->find, flen)) {
        e->sel_anchor = i;
        e->caret = i + flen;
        sync_caret_rc(e);
        ensure_visible(e);
        set_status(e, "found");
        return;
      }
      if (i == 0) break;
      i--;
    }
  }
  if (len >= flen) {
    i = len - flen;
    for (;;) {
      if (i < e->caret && buf_match(&e->buf, i, e->find, flen)) {
        e->sel_anchor = i;
        e->caret = i + flen;
        sync_caret_rc(e);
        ensure_visible(e);
        set_status(e, "found (wrap)");
        return;
      }
      if (i == 0) break;
      i--;
    }
  }
  set_status(e, "not found");
}

static size_t row_start(Editor *e, size_t row);

static void goto_line(Editor *e, size_t line1) {
  size_t row;
  ensure_lines(e);
  if (line1 == 0) line1 = 1;
  row = line1 - 1;
  if (e->lines.n && row >= e->lines.n) row = e->lines.n - 1;
  e->caret = row_start(e, row);
  clear_sel(e);
  sync_caret_rc(e);
  ensure_visible(e);
  set_status(e, "ok");
  mark(e);
}

static void insert_newline_indent(Editor *e) {
  size_t ls = line_start(e, e->caret), i = ls, n = 1;
  char ind[128];
  ind[0] = '\n';
  while (i < e->caret && n + 1 < sizeof ind) {
    char c = buf_at(&e->buf, i);
    if (c != ' ' && c != '\t') break;
    ind[n++] = c;
    i++;
  }
  insert_text(e, ind, n);
}

static void delete_line(Editor *e) {
  size_t a = line_start(e, e->caret), b = line_end(e, e->caret), len = buf_len(&e->buf);
  if (b < len && buf_at(&e->buf, b) == '\n') b++;
  else if (a > 0 && b == len) a--;
  if (b > a) push_delete(e, a, b - a);
  e->caret = a > buf_len(&e->buf) ? buf_len(&e->buf) : a;
  clear_sel(e);
  sync_caret_rc(e);
  ensure_visible(e);
}

static void dup_line(Editor *e) {
  size_t a = line_start(e, e->caret), b = line_end(e, e->caret), n;
  char *s;
  int need_nl = 0;
  if (b < buf_len(&e->buf) && buf_at(&e->buf, b) == '\n') b++;
  else need_nl = 1;
  n = b - a;
  s = slice_dup(e, a, b);
  if (!s) return;
  if (need_nl) {
    push_insert(e, b, "\n", 1, false);
    b++;
  }
  push_insert(e, b, s, n, false);
  free(s);
  e->caret = b + (e->caret - a) + (size_t)need_nl;
  clear_sel(e);
  sync_caret_rc(e);
  ensure_visible(e);
}

static void jump_bracket(Editor *e) {
  static const char op[] = "([{", cl[] = ")]}";
  size_t len = buf_len(&e->buf), pos = e->caret;
  char ch;
  int dir, depth, ix;
  const char *p;
  if (!len) return;
  if (pos >= len) pos = len - 1;
  ch = buf_at(&e->buf, pos);
  p = strchr(op, ch);
  if (p) {
    dir = 1;
    ix = (int)(p - op);
  } else {
    p = strchr(cl, ch);
    if (!p) {
      if (!pos) return;
      pos--;
      ch = buf_at(&e->buf, pos);
      p = strchr(op, ch);
      if (p) {
        dir = 1;
        ix = (int)(p - op);
      } else {
        p = strchr(cl, ch);
        if (!p) return;
        dir = -1;
        ix = (int)(p - cl);
      }
    } else {
      dir = -1;
      ix = (int)(p - cl);
    }
  }
  depth = 1;
  for (;;) {
    if (dir > 0) {
      if (pos + 1 >= len) return;
      pos++;
    } else {
      if (!pos) return;
      pos--;
    }
    ch = buf_at(&e->buf, pos);
    if (ch == op[ix]) {
      if (dir > 0) depth++;
      else if (--depth == 0) {
        move_caret(e, pos, false);
        return;
      }
    } else if (ch == cl[ix]) {
      if (dir < 0) depth++;
      else if (--depth == 0) {
        move_caret(e, pos, false);
        return;
      }
    }
  }
}

static void copy_sel(Editor *e, Plat *p) {
  char *s;
  size_t a, b;
  if (!has_sel(e)) return;
  a = sel_lo(e);
  b = sel_hi(e);
  s = slice_dup(e, a, b);
  if (!s) return;
  plat_clipboard_set(p, s, b - a);
  free(s);
  set_status(e, "copied");
}

static void cut_sel(Editor *e, Plat *p) {
  copy_sel(e, p);
  delete_sel(e);
  ensure_visible(e);
}

static void paste_clip(Editor *e, Plat *p) {
  size_t n = 0, room;
  char *s = plat_clipboard_get(p, &n);
  if (!s) return;
  room = MOTE_MAX_FILE - buf_len(&e->buf);
  if (n > room) {
    if (!room) {
      set_status(e, "file at size limit");
      free(s);
      return;
    }
    n = room;
    set_status(e, "paste truncated");
  }
  delete_sel(e);
  push_insert(e, e->caret, s, n, false); /* never coalesce paste */
  e->caret += n;
  clear_sel(e);
  sync_caret_rc(e);
  ensure_visible(e);
  free(s);
}

static size_t click_to_pos(Editor *e, int mx, int my) {
  size_t row, col;
  if (e->ch <= 0 || e->cw <= 0) return e->caret;
  if (my < 0) my = 0;
  if (e->rows > 0 && my >= e->rows * e->ch) my = e->rows * e->ch - 1;
  if (mx < 0) mx = 0;
  row = e->row0 + (size_t)(my / e->ch);
  col = e->col0 + (size_t)(mx / e->cw);
  return rc_to_pos(e, row, col);
}

bool ed_init(Editor *e) {
  memset(e, 0, sizeof *e);
  if (!buf_init(&e->buf, 0)) return false;
  undo_init(&e->undo);
  e->lines.dirty = true;
  set_status(e, "F1 help");
  e->need_draw = true;
  return true;
}

void ed_free(Editor *e) {
  buf_free(&e->buf);
  undo_free(&e->undo);
  free(e->lines.off);
  e->lines.off = NULL;
  e->lines.n = e->lines.capa = 0;
}

bool ed_open_path(Editor *e, const char *path) {
  FILE *f;
  Buf nb;
  if (e->dirty && e->mode != MODE_OPENASK) {
    snprintf(e->pending_path, sizeof e->pending_path, "%s", path);
    e->mode = MODE_OPENASK;
    set_status(e, "Unsaved! Ctrl+S save+open, Ctrl+Q discard+open, Esc");
    mark(e);
    return false;
  }
  f = mote_fopen(path, "rb");
  if (!f) {
    if (errno != ENOENT) {
      set_status(e, "open failed");
      return false;
    }
    buf_free(&e->buf);
    if (!buf_init(&e->buf, 0)) return false;
    snprintf(e->path, sizeof e->path, "%s", path);
    e->dirty = false;
    e->caret = e->sel_anchor = 0;
    e->row0 = e->col0 = 0;
    e->caret_row = e->caret_col = 0;
    lines_mark_dirty(e);
    undo_free(&e->undo);
    undo_init(&e->undo);
    set_status(e, "new file");
    return true;
  }
  fclose(f);
  if (!buf_init(&nb, 0)) {
    set_status(e, "out of memory");
    return false;
  }
  if (!buf_load(&nb, path)) {
    buf_free(&nb);
    set_status(e, "open failed (size/IO)");
    return false;
  }
  buf_free(&e->buf);
  e->buf = nb;
  snprintf(e->path, sizeof e->path, "%s", path);
  e->dirty = false;
  e->caret = e->sel_anchor = 0;
  e->row0 = e->col0 = 0;
  e->caret_row = e->caret_col = 0;
  lines_mark_dirty(e);
  undo_free(&e->undo);
  undo_init(&e->undo);
  set_status(e, "opened");
  return true;
}

static void prompt_enter(Editor *e) {
  if (e->mode == MODE_OPEN) {
    if (e->prompt[0]) ed_open_path(e, e->prompt);
    if (e->mode == MODE_OPENASK) return;
    e->mode = MODE_EDIT;
  } else if (e->mode == MODE_SAVEAS) {
    if (e->prompt[0]) save_to(e, e->prompt);
    e->mode = MODE_EDIT;
    if (e->pending_path[0] && !e->dirty) {
      char path[1024];
      snprintf(path, sizeof path, "%s", e->pending_path);
      e->pending_path[0] = 0;
      ed_open_path(e, path);
      e->mode = MODE_EDIT;
    }
  } else if (e->mode == MODE_FIND) {
    snprintf(e->find, sizeof e->find, "%s", e->prompt);
    e->mode = MODE_EDIT;
    find_next(e);
  } else if (e->mode == MODE_GOTO) {
    size_t line = (size_t)strtoul(e->prompt, NULL, 10);
    e->mode = MODE_EDIT;
    goto_line(e, line);
  }
  mark(e);
}

static void handle_prompt(Editor *e, const PlatEvent *ev) {
  size_t n;
  if (ev->type == PE_KEY) {
    if (ev->key == PK_ESCAPE) {
      e->mode = MODE_EDIT;
      set_status(e, "F1 help");
      return;
    }
    if (ev->key == PK_ENTER) { prompt_enter(e); return; }
    if (ev->key == PK_BACKSPACE) {
      n = strlen(e->prompt);
      if (n) {
        n = utf8_prev(e->prompt, n);
        e->prompt[n] = 0;
        mark(e);
      }
      return;
    }
  }
  if (ev->type == PE_TEXT && ev->text_len > 0 && e->mode != MODE_QUITASK &&
      e->mode != MODE_HELP && e->mode != MODE_OPENASK) {
    n = strlen(e->prompt);
    if (n + (size_t)ev->text_len < sizeof e->prompt - 1) {
      memcpy(e->prompt + n, ev->text, (size_t)ev->text_len);
      e->prompt[n + (size_t)ev->text_len] = 0;
      mark(e);
    }
  }
}

static void request_quit(Editor *e) {
  if (e->dirty) {
    e->mode = MODE_QUITASK;
    set_status(e, "Unsaved! Ctrl+S quit+save, Ctrl+Q discard, Esc");
    return;
  }
  e->want_quit = true;
}

void ed_handle(Editor *e, Plat *p, const PlatEvent *ev) {
  bool keep;
  size_t len, np;

  if (ev->type == PE_EXPOSE) { mark(e); return; }
  if (ev->type == PE_QUIT) { request_quit(e); return; }

  if (e->mode == MODE_HELP) {
    if (ev->type == PE_KEY || ev->type == PE_TEXT) {
      e->mode = MODE_EDIT;
      set_status(e, "F1 help");
    }
    return;
  }

  if (e->mode != MODE_EDIT) {
    if (e->mode == MODE_QUITASK && ev->type == PE_KEY) {
      if (ev->key == PK_QUIT) { e->want_quit = true; return; }
      if (ev->key == PK_SAVE) {
        try_save(e);
        if (!e->dirty) e->want_quit = true;
        return;
      }
      if (ev->key == PK_ESCAPE) {
        e->mode = MODE_EDIT;
        set_status(e, "F1 help");
        return;
      }
    }
    if (e->mode == MODE_OPENASK && ev->type == PE_KEY) {
      if (ev->key == PK_QUIT) {
        e->dirty = false;
        if (e->pending_path[0]) {
          char path[1024];
          snprintf(path, sizeof path, "%s", e->pending_path);
          e->pending_path[0] = 0;
          e->mode = MODE_EDIT;
          ed_open_path(e, path);
        }
        e->mode = MODE_EDIT;
        return;
      }
      if (ev->key == PK_SAVE) {
        try_save(e);
        if (!e->dirty && e->pending_path[0]) {
          char path[1024];
          snprintf(path, sizeof path, "%s", e->pending_path);
          e->pending_path[0] = 0;
          e->mode = MODE_EDIT;
          ed_open_path(e, path);
        } else if (e->dirty && !e->path[0]) {
          /* Save As path — keep pending */
          return;
        }
        e->mode = MODE_EDIT;
        return;
      }
      if (ev->key == PK_ESCAPE) {
        e->pending_path[0] = 0;
        e->mode = MODE_EDIT;
        set_status(e, "F1 help");
        return;
      }
    }
    handle_prompt(e, ev);
    return;
  }

  if (ev->type == PE_SCROLL) {
    size_t old = e->row0;
    int step = ev->wheel;
    if (step > 0)
      e->row0 = e->row0 > (size_t)step ? e->row0 - (size_t)step : 0;
    else if (step < 0)
      e->row0 += (size_t)(-step);
    if (e->row0 != old) view_invalid(e);
    mark(e);
    return;
  }

  if (ev->type == PE_MOUSE_DOWN) {
    np = click_to_pos(e, ev->mx, ev->my);
    e->mouse_down = true;
    e->caret = np;
    if (!ev->shift) e->sel_anchor = e->caret;
    sync_caret_rc(e);
    ensure_visible(e);
    mark(e);
    return;
  }
  if (ev->type == PE_MOUSE_UP) { e->mouse_down = false; return; }
  if (ev->type == PE_MOUSE_MOVE && e->mouse_down) {
    e->caret = click_to_pos(e, ev->mx, ev->my);
    sync_caret_rc(e);
    ensure_visible(e);
    mark(e);
    return;
  }

  if (ev->type == PE_TEXT && ev->text_len > 0 && !ev->ctrl) {
    insert_text(e, ev->text, (size_t)ev->text_len);
    return;
  }
  if (ev->type != PE_KEY) return;

  keep = ev->shift;
  len = buf_len(&e->buf);

  switch (ev->key) {
  case PK_LEFT:
    if (ev->ctrl) move_caret(e, prev_word(e, e->caret), keep);
    else move_caret(e, ed_prev(e, e->caret), keep);
    break;
  case PK_RIGHT:
    if (ev->ctrl) move_caret(e, next_word(e, e->caret), keep);
    else move_caret(e, ed_next(e, e->caret), keep);
    break;
  case PK_UP: move_vert(e, -1, keep); break;
  case PK_DOWN: move_vert(e, 1, keep); break;
  case PK_HOME: move_caret(e, line_start(e, e->caret), keep); break;
  case PK_END: move_caret(e, line_end(e, e->caret), keep); break;
  case PK_PGUP: move_vert(e, -(e->rows > 1 ? e->rows - 1 : 1), keep); break;
  case PK_PGDN: move_vert(e, e->rows > 1 ? e->rows - 1 : 1, keep); break;
  case PK_BACKSPACE:
    if (has_sel(e)) delete_sel(e);
    else if (e->caret > 0) {
      np = ed_prev(e, e->caret);
      push_delete(e, np, e->caret - np);
      e->caret = np;
      clear_sel(e);
      sync_caret_rc(e);
    }
    ensure_visible(e);
    mark(e);
    break;
  case PK_DELETE:
    if (has_sel(e)) delete_sel(e);
    else if (e->caret < len) {
      np = ed_next(e, e->caret);
      push_delete(e, e->caret, np - e->caret);
      clear_sel(e);
    }
    mark(e);
    break;
  case PK_ENTER: insert_newline_indent(e); break;
  case PK_TAB: insert_text(e, "\t", 1); break;
  case PK_SAVE: try_save(e); break;
  case PK_SAVEAS:
    e->mode = MODE_SAVEAS;
    e->prompt[0] = 0;
    set_status(e, "Save As — type path, Enter");
    break;
  case PK_OPEN:
    e->mode = MODE_OPEN;
    e->prompt[0] = 0;
    set_status(e, "Open — type path, Enter");
    break;
  case PK_QUIT: request_quit(e); break;
  case PK_UNDO: do_undo(e); break;
  case PK_REDO: do_redo(e); break;
  case PK_FIND:
    e->mode = MODE_FIND;
    snprintf(e->prompt, sizeof e->prompt, "%s", e->find);
    set_status(e, "Find — type, Enter");
    break;
  case PK_FINDNEXT: find_next(e); mark(e); break;
  case PK_FINDPREV: find_prev(e); mark(e); break;
  case PK_GOTO:
    e->mode = MODE_GOTO;
    e->prompt[0] = 0;
    set_status(e, "Go to line —");
    break;
  case PK_DUPLINE: dup_line(e); break;
  case PK_DELLINE: delete_line(e); break;
  case PK_BRACKET: jump_bracket(e); break;
  case PK_CUT: cut_sel(e, p); break;
  case PK_COPY: copy_sel(e, p); break;
  case PK_PASTE: paste_clip(e, p); break;
  case PK_SELALL:
    e->sel_anchor = 0;
    e->caret = len;
    sync_caret_rc(e);
    mark(e);
    break;
  case PK_HELP:
  case PK_F1:
    e->mode = MODE_HELP;
    mark(e);
    break;
  default:
    break;
  }
}

static void draw_range(Editor *e, Plat *p, size_t a, size_t b, int y) {
  size_t i, col = 0;
  size_t slo = sel_lo(e), shi = sel_hi(e);
  bool selecting = has_sel(e);

  for (i = a; i < b;) {
    uint32_t cp;
    char chunk[4], chs[4];
    int n, wcols = 1, enc, k;
    size_t rem = b - i;
    for (k = 0; k < 4 && (size_t)k < rem; k++)
      chunk[k] = buf_at(&e->buf, i + (size_t)k);
    n = utf8_decode(chunk, rem < 4 ? rem : 4, &cp);
    if (n <= 0) {
      n = 1;
      cp = '?';
    }
    if (cp == '\t') wcols = tab_cols(col);
    if (col + (size_t)wcols > e->col0 && col < e->col0 + (size_t)e->cols) {
      int x = (int)(col - e->col0) * e->cw;
      if (selecting && i >= slo && i < shi)
        plat_fill_rect(p, x, y, e->cw * wcols, e->ch, COL_SEL);
      if (cp != '\t') {
        enc = utf8_encode(cp, chs);
        plat_draw_text(p, x, y, chs, enc, COL_FG);
      }
    }
    i += (size_t)n;
    col += (size_t)wcols;
    if (col >= e->col0 + (size_t)e->cols + 8) break;
  }
}

static size_t row_start(Editor *e, size_t row) {
  ensure_lines(e);
  if (!e->lines.n) return 0;
  if (row >= e->lines.n) return buf_len(&e->buf);
  return e->lines.off[row];
}

void ed_draw(Editor *e, Plat *p) {
  int w, h, i, sw;
  size_t crow, ccol, pos, a, b, len;
  char bar[384];
  const char *name;
  static const char *help[] = {
      "mote — SYFaren",
      "",
      "Files",
      "  Ctrl+S / Shift+S     save / save as",
      "  Ctrl+O / Q           open / quit",
      "",
      "Edit",
      "  Ctrl+Z / Y           undo / redo",
      "  Ctrl+F   F3 / S-F3   find · next / prev",
      "  Ctrl+G / D / ]       line · dup · bracket",
      "  Ctrl+Shift+K         delete line",
      "  Ctrl+X / C / V / A   cut copy paste all",
      "  Tab · Enter          tab · newline+indent",
      "",
      "Move    arrows · Home/End · PgUp/Dn · mouse",
      "Unsaved Ctrl+S save · Ctrl+Q discard · Esc",
  };

  plat_get_size(p, &w, &h);
  e->cw = plat_font_w(p);
  e->ch = plat_font_h(p);
  if (e->cw < 1) e->cw = 8;
  if (e->ch < 1) e->ch = 16;
  e->cols = w / e->cw;
  e->rows = (h / e->ch) - 1;
  if (e->rows < 1) e->rows = 1;

  plat_begin_frame(p);
  plat_clear(p, COL_BG);

  len = buf_len(&e->buf);
  if (!e->row0_valid) {
    e->row0_pos = row_start(e, e->row0);
    e->row0_valid = true;
  }
  pos = e->row0_pos;
  for (i = 0; i < e->rows; i++) {
    a = pos;
    b = line_end(e, a);
    draw_range(e, p, a, b, i * e->ch);
    if (b < len && buf_at(&e->buf, b) == '\n') pos = b + 1;
    else break;
  }

  crow = e->caret_row;
  ccol = e->caret_col;
  if (e->mode != MODE_HELP && crow >= e->row0 &&
      crow < e->row0 + (size_t)e->rows && ccol >= e->col0 &&
      ccol < e->col0 + (size_t)e->cols) {
    int cx = (int)(ccol - e->col0) * e->cw;
    int cy = (int)(crow - e->row0) * e->ch;
    if (!plat_set_caret(p, cx, cy, e->ch, true))
      plat_fill_rect(p, cx, cy, 2, e->ch, COL_FG);
  } else {
    plat_set_caret(p, 0, 0, e->ch, false);
  }

  sw = e->rows * e->ch;
  plat_fill_rect(p, 0, sw, w, e->ch, COL_STAT);
  name = e->path[0] ? e->path : "[untitled]";
  if (e->mode == MODE_OPEN || e->mode == MODE_SAVEAS || e->mode == MODE_FIND ||
      e->mode == MODE_GOTO)
    snprintf(bar, sizeof bar, "%s %s", e->status, e->prompt);
  else if (e->mode == MODE_QUITASK || e->mode == MODE_OPENASK)
    snprintf(bar, sizeof bar, "%s", e->status);
  else {
    const char *base = strrchr(name, '/');
    const char *b2 = strrchr(name, '\\');
    if (b2 && (!base || b2 > base)) base = b2;
    base = base ? base + 1 : name;
    snprintf(bar, sizeof bar, "%s%s  %zu:%zu  %s", base, e->dirty ? "*" : "",
             crow + 1, ccol + 1, e->status[0] ? e->status : "F1 help");
  }
  plat_draw_text(p, 4, sw, bar, (int)strlen(bar), COL_STATFG);

  if (e->mode == MODE_HELP) {
    int nlines = (int)(sizeof help / sizeof help[0]);
    int box_h = (nlines + 2) * e->ch;
    int box_y = e->ch;
    int box_w = w - 40;
    if (box_w < 200) box_w = w - 8;
    plat_fill_rect(p, 16, box_y - 4, box_w + 8, box_h + 8, COL_HELPBD);
    plat_fill_rect(p, 20, box_y, box_w, box_h, COL_HELPBG);
    for (i = 0; i < nlines; i++)
      plat_draw_text(p, 28, box_y + (i + 1) * e->ch, help[i],
                     (int)strlen(help[i]), COL_FG);
  }

  {
    static char last[260];
    char title[260];
    const char *base = e->path[0] ? e->path : "untitled";
    const char *slash = strrchr(base, '/');
    const char *b2 = strrchr(base, '\\');
    if (b2 && (!slash || b2 > slash)) slash = b2;
    if (slash) base = slash + 1;
    snprintf(title, sizeof title, "%s%s — mote", e->dirty ? "*" : "", base);
    if (strcmp(title, last) != 0) {
      snprintf(last, sizeof last, "%s", title);
      plat_set_title(p, title);
    }
  }

  plat_end_frame(p);
  e->need_draw = false;
}
