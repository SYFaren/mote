/* mote core — editor.c */
#include "editor.h"
#include "theme.h"
#include "hl.h"
#include "utf8.h"
#include "regex.h"
#include "dirlist.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <errno.h>
#include "mote_snprintf.h"

#define D(e) (&(e)->docs[(e)->cur])
#define NO_POS ((size_t)-1) /* unset bookmark / bracket / search position */

static const char *path_base(const char *name) {
  const char *a, *b, *best;
  if (!name || !name[0]) return name ? name : "";
  a = strrchr(name, '/');
  b = strrchr(name, '\\');
  best = name;
  if (a && a + 1 > best) best = a + 1;
  if (b && b + 1 > best) best = b + 1;
  return best;
}


static void mark(Editor *e) { e->need_draw = MOTE_TRUE; }
static void vrow_invalidate(Editor *e) { e->vrow_n = 0; }

static const Theme *th(Editor *e) { return theme_get(e->theme_id); }

static mote_u32 hl_color(const Theme *t, HlKind k) {
  switch (k) {
  case HL_COMMENT: return t->comment;
  case HL_STRING: return t->str;
  case HL_NUMBER: return t->number;
  case HL_KEYWORD: return t->kw;
  case HL_TYPE: return t->type;
  case HL_PREPROC: return t->preproc;
  case HL_MATCH: return t->match;
  case HL_BRACKET: return t->bracket;
  default: return t->fg;
  }
}

static size_t count_nl(const char *s, size_t n) {
  size_t i, c = 0;
  for (i = 0; i < n; i++)
    if (s[i] == '\n') c++;
  return c;
}

static size_t count_nl_buf(const Buf *b) {
  size_t i, c = 0, len = buf_len(b);
  for (i = 0; i < len; i++)
    if (buf_at(b, i) == '\n') c++;
  return c;
}

static void lines_mark_dirty(Doc *d) {
  d->lines.dirty = MOTE_TRUE;
  d->hl_ml_valid = MOTE_FALSE;
}

static void lines_shift(Doc *d, size_t pos, long delta) {
  size_t i;
  if (d->lines.dirty || !d->lines.off || !delta) return;
  for (i = 0; i < d->lines.n; i++) {
    if (d->lines.off[i] > pos)
      d->lines.off[i] = (size_t)((long)d->lines.off[i] + delta);
  }
  d->hl_ml_valid = MOTE_FALSE; /* the edit may open or close a block comment */
}

static mote_bool lines_push(LineMap *m, size_t off) {
  size_t *no;
  if (m->n == m->capa) {
    size_t nc = m->capa ? m->capa * 2 : 64;
    no = (size_t *)realloc(m->off, nc * sizeof(size_t));
    if (!no) return MOTE_FALSE;
    m->off = no;
    m->capa = nc;
  }
  m->off[m->n++] = off;
  return MOTE_TRUE;
}

static void lines_rebuild(Doc *d) {
  size_t i, len = buf_len(&d->buf);
  d->lines.n = 0;
  if (!lines_push(&d->lines, 0)) return;
  for (i = 0; i < len; i++) {
    if (buf_at(&d->buf, i) == '\n') {
      if (!lines_push(&d->lines, i + 1)) return;
    }
  }
  d->lines.dirty = MOTE_FALSE;
}

static void ensure_lines(Doc *d) {
  if (d->lines.dirty || !d->lines.off) lines_rebuild(d);
}

static void set_status(Editor *e, const char *s) {
  mote_snprintf(e->status, sizeof e->status, "%s", s ? s : "");
  mark(e);
}

static void set_statusf(Editor *e, const char *fmt, ...) {
  va_list ap;
  va_start(ap, fmt);
  mote_vsnprintf(e->status, sizeof e->status, fmt, ap);
  va_end(ap);
  mark(e);
}

static void toggle_flag(Editor *e, mote_bool *flag, const char *name) {
  *flag = !*flag;
  set_statusf(e, "%s %s", name, *flag ? "on" : "off");
}

/* Edits are refused on read-only docs; says so in the status bar. */
static mote_bool editable(Editor *e, const Doc *d) {
  if (!d->readonly) return MOTE_TRUE;
  set_status(e, "readonly");
  return MOTE_FALSE;
}

static void begin_mode(Editor *e, EdMode mode, const char *status) {
  e->mode = mode;
  e->prompt[0] = 0;
  set_status(e, status);
}

static void leave_mode(Editor *e) {
  e->mode = MODE_EDIT;
  set_status(e, "F1 help");
}

static void unsaved_ask(Editor *e, EdMode mode, const char *verb) {
  e->mode = mode;
  e->prompt[0] = 0;
  set_statusf(e, "Unsaved — ^S %s  ^Q discard  Esc", verb);
}

static const char *prompt_bar_prefix(EdMode mode) {
  switch (mode) {
  case MODE_OPEN:
    return "Open:";
  case MODE_SAVEAS:
    return "Save As:";
  case MODE_FIND:
    return "Find (/re/ or text, Alt+C case, Alt+W word):";
  case MODE_REPLACE:
    return "Replace (/find/repl/ or text):";
  case MODE_GOTO:
    return "Goto:";
  case MODE_QUICKOPEN:
    return "Go to file — type filter, j/k Enter:";
  default:
    return "";
  }
}


static size_t sel_lo(const Doc *d) {
  return d->caret < d->sel_anchor ? d->caret : d->sel_anchor;
}
static size_t sel_hi(const Doc *d) {
  return d->caret > d->sel_anchor ? d->caret : d->sel_anchor;
}
static mote_bool has_sel(const Doc *d) { return d->caret != d->sel_anchor; }
static void clear_sel(Doc *d) { d->sel_anchor = d->caret; }

static char *slice_dup(const Doc *d, size_t a, size_t b) {
  size_t n = b - a;
  char *s = (char *)malloc(n + 1);
  if (!s) return NULL;
  buf_get(&d->buf, a, n, s);
  s[n] = 0;
  return s;
}

static int tab_cols(size_t col) {
  int w = 4 - (int)(col % 4);
  return w <= 0 ? 4 : w;
}

static size_t disp_advance(Doc *d, size_t i, size_t stop, size_t *col,
                           size_t want, int stop_want) {
  size_t len = buf_len(&d->buf);
  if (stop > len) stop = len;
  while (i < stop) {
    char chunk[4];
    mote_u32 cp;
    int n, k, w;
    size_t rem = len - i;
    for (k = 0; k < 4 && (size_t)k < rem; k++)
      chunk[k] = buf_at(&d->buf, i + (size_t)k);
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

static size_t disp_col_between(Doc *d, size_t start, size_t pos) {
  size_t col = 0;
  disp_advance(d, start, pos, &col, 0, 0);
  return col;
}

static size_t pos_at_disp_col(Doc *d, size_t start, size_t end, size_t want) {
  size_t col = 0;
  return disp_advance(d, start, end, &col, want, 1);
}

static void pos_to_rc(Doc *d, size_t pos, size_t *row, size_t *col) {
  size_t lo, hi, len = buf_len(&d->buf);
  ensure_lines(d);
  if (pos > len) pos = len;
  if (!d->lines.n) {
    *row = 0;
    *col = disp_col_between(d, 0, pos);
    return;
  }
  lo = 0;
  hi = d->lines.n;
  while (lo + 1 < hi) {
    size_t mid = lo + (hi - lo) / 2;
    if (d->lines.off[mid] <= pos) lo = mid;
    else hi = mid;
  }
  *row = lo;
  *col = disp_col_between(d, d->lines.off[lo], pos);
}

static size_t row_start(Doc *d, size_t row) {
  ensure_lines(d);
  if (!d->lines.n) return 0;
  if (row >= d->lines.n) return buf_len(&d->buf);
  return d->lines.off[row];
}

static void bm_clamp_rows(Doc *d) {
  int i;
  ensure_lines(d);
  if (!d->lines.n) return;
  for (i = 0; i < MAX_BOOKMARKS; i++)
    if (d->bm_row[i] != NO_POS && d->bm_row[i] >= d->lines.n)
      d->bm_row[i] = d->lines.n - 1;
}

static void bm_line_insert(Doc *d, size_t pos, size_t nl_count) {
  size_t er, ec, rs;
  int i;
  if (!nl_count) return;
  ensure_lines(d);
  pos_to_rc(d, pos, &er, &ec);
  rs = row_start(d, er);
  for (i = 0; i < MAX_BOOKMARKS; i++) {
    size_t br = d->bm_row[i];
    if (br == NO_POS) continue;
    if (br > er)
      d->bm_row[i] = br + nl_count;
    else if (br == er && pos > rs)
      d->bm_row[i] = br + nl_count;
  }
}

static void bm_line_delete(Doc *d, size_t pos, size_t nl_count) {
  size_t er, ec;
  int i;
  if (!nl_count) return;
  ensure_lines(d);
  pos_to_rc(d, pos, &er, &ec);
  for (i = 0; i < MAX_BOOKMARKS; i++) {
    size_t br = d->bm_row[i];
    if (br == NO_POS) continue;
    if (br > er + nl_count)
      d->bm_row[i] = br - nl_count;
    else if (br > er)
      d->bm_row[i] = er;
  }
  bm_clamp_rows(d);
}

static size_t rc_to_pos(Doc *d, size_t row, size_t col) {
  size_t start, end, len = buf_len(&d->buf);
  ensure_lines(d);
  if (!d->lines.n) return 0;
  if (row >= d->lines.n) return len;
  start = d->lines.off[row];
  if (row + 1 < d->lines.n)
    end = d->lines.off[row + 1] > 0 ? d->lines.off[row + 1] - 1 : 0;
  else
    end = len;
  return pos_at_disp_col(d, start, end, col);
}

static size_t line_start(const Doc *d, size_t pos) {
  while (pos > 0 && buf_at(&d->buf, pos - 1) != '\n') pos--;
  return pos;
}
static size_t line_end(const Doc *d, size_t pos) {
  size_t len = buf_len(&d->buf);
  while (pos < len && buf_at(&d->buf, pos) != '\n') pos++;
  return pos;
}

static size_t line_width(Doc *d, size_t row) {
  size_t a = row_start(d, row);
  return disp_col_between(d, a, line_end(d, a));
}

static size_t segs_of(Editor *e, Doc *d, size_t row) {
  size_t w;
  if (!e->wrap || e->cols < 1) return 1;
  w = line_width(d, row);
  return w == 0 ? 1 : (w + (size_t)e->cols - 1) / (size_t)e->cols;
}

/* First visual (wrapped) row of logical row `row`. `vp` is an optional
   prefix-sum table from build_vrow_prefix; without it rows are summed. */
static size_t vrow_of_row(Editor *e, Doc *d, size_t row, const size_t *vp) {
  size_t r, v = 0;
  if (vp) return vp[row];
  for (r = 0; r < row; r++) v += segs_of(e, d, r);
  return v;
}

static size_t view_vrow0(Editor *e, Doc *d, const size_t *vp) {
  return vrow_of_row(e, d, d->row0, vp) + d->wrap0;
}

static void caret_vis(Editor *e, Doc *d, size_t *vr, size_t *vc, const size_t *vp) {
  size_t cols = (size_t)e->cols;
  *vr = vrow_of_row(e, d, d->caret_row, vp);
  *vc = d->caret_col;
  if (!e->wrap || e->cols < 1) return;
  if (d->caret_col > 0 && d->caret_col % cols == 0) {
    *vr += d->caret_col / cols - 1;
    *vc = cols; /* past last cell of segment */
  } else {
    *vr += d->caret_col / cols;
    *vc = d->caret_col % cols;
  }
}

static size_t *build_vrow_prefix(Editor *e, Doc *d, size_t nlines) {
  size_t r, *vp;
  if (!e->wrap || e->cols < 1 || nlines == 0) return NULL;
  vp = (size_t *)malloc((nlines + 1) * sizeof(size_t));
  if (!vp) return NULL;
  vp[0] = 0;
  for (r = 0; r < nlines; r++) vp[r + 1] = vp[r] + segs_of(e, d, r);
  return vp;
}

/* Scroll so visual row `want` is on top, clamped to the last segment. */
static void set_view_vrow(Editor *e, Doc *d, size_t want) {
  size_t r, s = 1, acc = 0, n;
  ensure_lines(d);
  n = d->lines.n ? d->lines.n : 1;
  for (r = 0; r < n; r++) {
    s = segs_of(e, d, r);
    if (acc + s > want) break;
    acc += s;
  }
  if (r == n) {
    r = n - 1;
    acc -= s;
    want = acc + s - 1;
  }
  d->row0 = r;
  d->wrap0 = want - acc;
}

static void sync_caret_rc(Doc *d) {
  pos_to_rc(d, d->caret, &d->caret_row, &d->caret_col);
  d->pref_col = d->caret_col;
}

static void ensure_visible(Editor *e, Doc *d) {
  size_t vr, vc, top;
  if (e->wrap) d->col0 = 0;
  caret_vis(e, d, &vr, &vc, NULL);
  top = view_vrow0(e, d, NULL);
  if (vr < top)
    set_view_vrow(e, d, vr);
  else if (e->rows > 0 && vr >= top + (size_t)e->rows)
    set_view_vrow(e, d, vr - (size_t)e->rows + 1);
  if (!e->wrap) {
    if (d->caret_col < d->col0) d->col0 = d->caret_col;
    if (e->cols > 0 && d->caret_col >= d->col0 + (size_t)e->cols)
      d->col0 = d->caret_col - (size_t)e->cols + 1;
  }
}

static void clamp_caret(Doc *d) {
  size_t len = buf_len(&d->buf);
  if (d->caret > len) d->caret = len;
  if (d->sel_anchor > len) d->sel_anchor = len;
}

/* Every buffer edit goes through push_delete / push_insert: they record
   undo, keep bookmarks on their lines and patch the line map. */
static mote_bool push_delete(Editor *e, Doc *d, size_t pos, size_t n) {
  char *t;
  size_t nl;
  if (!n || d->readonly) return MOTE_FALSE;
  t = (char *)malloc(n);
  if (!t) {
    set_status(e, "out of memory");
    return MOTE_FALSE;
  }
  buf_get(&d->buf, pos, n, t);
  nl = count_nl(t, n);
  if (!undo_push(&d->undo, U_DELETE, pos, t, n, MOTE_FALSE)) {
    free(t);
    set_status(e, "out of memory");
    return MOTE_FALSE;
  }
  free(t);
  bm_line_delete(d, pos, nl);
  buf_delete(&d->buf, pos, n);
  d->dirty = MOTE_TRUE;
  clamp_caret(d);
  vrow_invalidate(e);
  if (nl) lines_mark_dirty(d);
  else lines_shift(d, pos, -(long)n);
  mark(e);
  return MOTE_TRUE;
}

static mote_bool push_insert(Editor *e, Doc *d, size_t pos, const char *s, size_t n,
                             mote_bool coalesce) {
  size_t nl;
  if (!n || !s || d->readonly) return MOTE_FALSE;
  nl = count_nl(s, n);
  bm_line_insert(d, pos, nl);
  if (!buf_insert(&d->buf, pos, s, n)) {
    set_status(e, "out of memory");
    return MOTE_FALSE;
  }
  if (!undo_push(&d->undo, U_INSERT, pos, s, n, coalesce)) {
    buf_delete(&d->buf, pos, n);
    set_status(e, "out of memory");
    return MOTE_FALSE;
  }
  d->dirty = MOTE_TRUE;
  clamp_caret(d);
  vrow_invalidate(e);
  if (nl) {
    lines_mark_dirty(d);
    bm_clamp_rows(d);
  } else {
    lines_shift(d, pos, (long)n);
  }
  mark(e);
  return MOTE_TRUE;
}

/* Put the caret at `pos` (extending the selection if `keep_sel`) and
   scroll it into view. */
static void move_caret(Editor *e, Doc *d, size_t pos, mote_bool keep_sel) {
  d->caret = pos;
  clamp_caret(d);
  buf_seek(&d->buf, d->caret);
  if (!keep_sel) clear_sel(d);
  sync_caret_rc(d);
  ensure_visible(e, d);
  mark(e);
}

static void place_caret(Editor *e, Doc *d, size_t pos) {
  move_caret(e, d, pos, MOTE_FALSE);
}

static void delete_sel(Editor *e, Doc *d) {
  size_t a, b;
  if (!has_sel(d) || d->readonly) return;
  a = sel_lo(d);
  b = sel_hi(d);
  push_delete(e, d, a, b - a);
  d->caret = a;
  clear_sel(d);
  sync_caret_rc(d);
}

/* Replace the selection (if any) with `s` and put the caret after it. */
static void insert_at_caret(Editor *e, Doc *d, const char *s, size_t n,
                            mote_bool coalesce) {
  if (!editable(e, d)) return;
  delete_sel(e, d);
  if (push_insert(e, d, d->caret, s, n, coalesce)) d->caret += n;
  place_caret(e, d, d->caret);
}

static void insert_text(Editor *e, Doc *d, const char *s, size_t n) {
  insert_at_caret(e, d, s, n, n > 0 && n <= 4 && s[0] != '\n' && s[0] != '\t');
}

/* Typing an opener inserts the pair; with a selection it wraps it. */
static void insert_autoclose(Editor *e, Doc *d, char open, char close) {
  char pair[2];
  size_t lo, hi;
  if (!editable(e, d)) return;
  pair[0] = open;
  pair[1] = close;
  if (!has_sel(d)) {
    if (push_insert(e, d, d->caret, pair, 2, MOTE_FALSE)) d->caret++;
    place_caret(e, d, d->caret);
    return;
  }
  lo = sel_lo(d);
  hi = sel_hi(d);
  if (!push_insert(e, d, lo, &open, 1, MOTE_FALSE)) return;
  if (!push_insert(e, d, hi + 1, &close, 1, MOTE_FALSE)) return;
  place_caret(e, d, lo + 1);
}

static void apply_undo_act(Editor *e, Doc *d, UndoAct *a, mote_bool redo) {
  if (a->kind == (redo ? U_INSERT : U_DELETE)) {
    bm_line_insert(d, a->pos, count_nl(a->text, a->len));
    if (!buf_insert(&d->buf, a->pos, a->text, a->len)) {
      if (redo) d->undo.head--;
      else d->undo.head++;
      set_status(e, redo ? "redo failed" : "undo failed");
      return;
    }
    d->caret = a->pos + a->len;
  } else {
    bm_line_delete(d, a->pos, count_nl(a->text, a->len));
    buf_delete(&d->buf, a->pos, a->len);
    d->caret = a->pos;
  }
  d->dirty = MOTE_TRUE;
  lines_mark_dirty(d);
  vrow_invalidate(e);
  place_caret(e, d, d->caret);
}

static void do_undo(Editor *e, Doc *d, mote_bool redo) {
  UndoAct *a;
  if (!editable(e, d)) return;
  a = redo ? undo_pop_redo(&d->undo) : undo_pop_undo(&d->undo);
  if (a) apply_undo_act(e, d, a, redo);
}

static size_t ed_prev(const Doc *d, size_t i) {
  if (!i) return 0;
  i--;
  while (i && ((unsigned char)buf_at(&d->buf, i) & 0xC0) == 0x80) i--;
  return i;
}

static size_t ed_next(const Doc *d, size_t i) {
  char t[4];
  mote_u32 cp;
  size_t len = buf_len(&d->buf);
  int got, n;
  if (i >= len) return len;
  got = (int)(len - i);
  if (got > 4) got = 4;
  buf_get(&d->buf, i, (size_t)got, t);
  n = utf8_decode(t, (size_t)got, &cp);
  return i + (size_t)(n > 0 ? n : 1);
}

static int is_word(unsigned char c) {
  return isalnum(c) || c == '_' || c >= 0x80;
}

static size_t next_word(const Doc *d, size_t p) {
  size_t len = buf_len(&d->buf);
  while (p < len && !is_word((unsigned char)buf_at(&d->buf, p))) p = ed_next(d, p);
  while (p < len && is_word((unsigned char)buf_at(&d->buf, p))) p = ed_next(d, p);
  return p;
}

static size_t prev_word(const Doc *d, size_t p) {
  if (!p) return 0;
  p = ed_prev(d, p);
  while (p > 0 && !is_word((unsigned char)buf_at(&d->buf, p))) p = ed_prev(d, p);
  while (p > 0) {
    size_t q = ed_prev(d, p);
    if (!is_word((unsigned char)buf_at(&d->buf, q))) break;
    p = q;
  }
  return p;
}

static void vis_to_pos(Editor *e, Doc *d, size_t vr, size_t vc, size_t *row,
                       size_t *col) {
  size_t r = 0, acc = 0, n, s, seg;
  ensure_lines(d);
  n = d->lines.n ? d->lines.n : 1;
  if (e->wrap && e->cols > 0 && vc > (size_t)e->cols) vc = (size_t)e->cols;
  while (r < n) {
    s = segs_of(e, d, r);
    if (acc + s > vr) {
      seg = vr - acc;
      *row = r;
      if (e->wrap && e->cols > 0)
        *col = seg * (size_t)e->cols + vc;
      else
        *col = vc;
      return;
    }
    acc += s;
    r++;
  }
  *row = n ? n - 1 : 0;
  *col = line_width(d, *row);
}

static void move_vert(Editor *e, Doc *d, int dy, mote_bool keep_sel) {
  size_t vr, vc, nrow, ncol, want;
  caret_vis(e, d, &vr, &vc, NULL);
  if (e->wrap && e->cols > 0)
    want = d->pref_col % (size_t)e->cols;
  else
    want = d->pref_col;
  if (dy < 0 && vr == 0) {
    d->caret = 0;
    d->caret_row = d->caret_col = 0;
    if (!keep_sel) clear_sel(d);
    ensure_visible(e, d);
    mark(e);
    return;
  }
  if (dy < 0)
    vr -= (size_t)(-dy);
  else
    vr += (size_t)dy;
  vis_to_pos(e, d, vr, want, &nrow, &ncol);
  d->caret = rc_to_pos(d, nrow, ncol);
  clamp_caret(d);
  pos_to_rc(d, d->caret, &d->caret_row, &d->caret_col);
  if (e->wrap && e->cols > 0) {
    size_t seg = d->caret_col / (size_t)e->cols;
    d->pref_col = seg * (size_t)e->cols + want;
  } else
    d->pref_col = want;
  if (!keep_sel) clear_sel(d);
  ensure_visible(e, d);
  mark(e);
}

static void recent_add(Editor *e, const char *path) {
  int i;
  if (!path || !path[0]) return;
  for (i = 0; i < e->nrecent; i++) {
    if (strcmp(e->recent[i], path) == 0) {
      char tmp[1024];
      mote_snprintf(tmp, sizeof tmp, "%s", e->recent[i]);
      memmove(e->recent[1], e->recent[0], (size_t)i * sizeof e->recent[0]);
      mote_snprintf(e->recent[0], sizeof e->recent[0], "%s", tmp);
      return;
    }
  }
  if (e->nrecent < MAX_RECENT) e->nrecent++;
  memmove(e->recent[1], e->recent[0],
          (size_t)(e->nrecent - 1) * sizeof e->recent[0]);
  mote_snprintf(e->recent[0], sizeof e->recent[0], "%s", path);
}

/* Copy `src` into a fresh `out`, expanding LF to CRLF or collapsing
   CRLF / lone CR to LF. */
static mote_bool buf_convert_eol(const Buf *src, Buf *out, mote_bool to_crlf) {
  size_t i, w = 0, len = buf_len(src);
  size_t cap = to_crlf ? len + count_nl_buf(src) : len;
  char *tmp = (char *)malloc(cap + 1);
  mote_bool ok;
  if (!tmp) return MOTE_FALSE;
  for (i = 0; i < len; i++) {
    char c = buf_at(src, i);
    if (to_crlf) {
      if (c == '\n') tmp[w++] = '\r';
      tmp[w++] = c;
    } else if (c != '\r') {
      tmp[w++] = c;
    } else if (i + 1 >= len || buf_at(src, i + 1) != '\n') {
      tmp[w++] = '\n';
    }
  }
  ok = buf_init(out, w);
  if (ok && w && !buf_insert(out, 0, tmp, w)) {
    buf_free(out);
    ok = MOTE_FALSE;
  }
  free(tmp);
  return ok;
}

/* Remember the file's line ending and keep only LF in memory. */
static void normalize_eol(Doc *d) {
  size_t i, len = buf_len(&d->buf);
  mote_bool saw_cr = MOTE_FALSE;
  Buf nb;
  d->eol = EOL_LF;
  for (i = 0; i < len; i++) {
    if (buf_at(&d->buf, i) != '\r') continue;
    saw_cr = MOTE_TRUE;
    if (i + 1 < len && buf_at(&d->buf, i + 1) == '\n') {
      d->eol = EOL_CRLF;
      break;
    }
  }
  if (!saw_cr || !buf_convert_eol(&d->buf, &nb, MOTE_FALSE)) return;
  buf_free(&d->buf);
  d->buf = nb;
}

static mote_bool save_to(Editor *e, Doc *d, const char *path) {
  mote_bool ok;
  Buf crlf;
  if (!path || !path[0]) {
    set_status(e, "empty path");
    return MOTE_FALSE;
  }
  if (d->eol == EOL_CRLF) {
    if (!buf_convert_eol(&d->buf, &crlf, MOTE_TRUE)) {
      set_status(e, "out of memory");
      return MOTE_FALSE;
    }
    ok = buf_save(&crlf, path);
    buf_free(&crlf);
  } else {
    ok = buf_save(&d->buf, path);
  }
  if (!ok) {
    set_status(e, "save failed");
    return MOTE_FALSE;
  }
  mote_snprintf(d->path, sizeof d->path, "%s", path);
  d->dirty = MOTE_FALSE;
  recent_add(e, path);
  set_status(e, "saved");
  return MOTE_TRUE;
}

static void try_save(Editor *e, Doc *d) {
  if (d->path[0]) save_to(e, d, d->path);
  else begin_mode(e, MODE_SAVEAS, "Save As:");
}

static mote_bool match_at(Editor *e, Doc *d, size_t i, size_t *out_len) {
  size_t len = buf_len(&d->buf);
  size_t flen;
  if (!e->find[0]) return MOTE_FALSE;
  if (e->find_regex) {
    flen = re_match_buf(&d->buf, i, e->find, !e->find_case);
    if (!flen) return MOTE_FALSE;
  } else {
    flen = strlen(e->find);
    if (i + flen > len) return MOTE_FALSE;
    if (e->find_case) {
      if (!buf_match(&d->buf, i, e->find, flen)) return MOTE_FALSE;
    } else {
      if (!buf_match_ci(&d->buf, i, e->find, flen)) return MOTE_FALSE;
    }
  }
  if (e->find_word) {
    if (i > 0 && is_word((unsigned char)buf_at(&d->buf, i - 1))) return MOTE_FALSE;
    if (i + flen < len && is_word((unsigned char)buf_at(&d->buf, i + flen)))
      return MOTE_FALSE;
  }
  if (out_len) *out_len = flen;
  return MOTE_TRUE;
}

static void apply_match(Editor *e, Doc *d, size_t i, size_t flen, const char *msg) {
  d->sel_anchor = i;
  d->caret = i + flen;
  d->match_a = i;
  d->match_b = i + flen;
  sync_caret_rc(d);
  ensure_visible(e, d);
  set_status(e, msg);
}

/* First (or, with `last`, final) match starting in [from, to). */
static mote_bool scan_match(Editor *e, Doc *d, size_t from, size_t to,
                            mote_bool last, size_t *pos, size_t *mlen) {
  size_t i, n;
  mote_bool found = MOTE_FALSE;
  for (i = from; i < to; i = ed_next(d, i)) {
    if (!match_at(e, d, i, &n)) continue;
    *pos = i;
    *mlen = n;
    found = MOTE_TRUE;
    if (!last) break;
  }
  return found;
}

static mote_bool find_ready(Editor *e, Doc *d) {
  if (!e->find[0]) {
    set_status(e, "no pattern");
    return MOTE_FALSE;
  }
  if (!buf_len(&d->buf)) {
    set_status(e, "not found");
    return MOTE_FALSE;
  }
  return MOTE_TRUE;
}

static void find_not_found(Editor *e, Doc *d) {
  d->match_a = d->match_b = 0;
  set_status(e, "not found");
}

static void find_next(Editor *e, Doc *d) {
  size_t len, start, wrap_end, i, mlen;
  if (!find_ready(e, d)) return;
  len = buf_len(&d->buf);
  start = d->caret < len ? ed_next(d, d->caret) : len;
  wrap_end = d->caret < len ? d->caret + 1 : len;
  if (scan_match(e, d, start, len, MOTE_FALSE, &i, &mlen))
    apply_match(e, d, i, mlen, "found");
  else if (scan_match(e, d, 0, wrap_end, MOTE_FALSE, &i, &mlen))
    apply_match(e, d, i, mlen, "found (wrap)");
  else
    find_not_found(e, d);
}

static void find_prev(Editor *e, Doc *d) {
  size_t lim, i, mlen;
  if (!find_ready(e, d)) return;
  lim = has_sel(d) ? sel_lo(d) : d->caret;
  if (scan_match(e, d, 0, lim, MOTE_TRUE, &i, &mlen))
    apply_match(e, d, i, mlen, "found");
  else if (scan_match(e, d, 0, buf_len(&d->buf), MOTE_TRUE, &i, &mlen))
    apply_match(e, d, i, mlen, "found (wrap)");
  else
    find_not_found(e, d);
}

static const char bracket_open[] = "([{";
static const char bracket_close[] = ")]}";

/* Bracket kind (index into bracket_open / bracket_close) and the scan
   direction towards its partner: +1 from an opener, -1 from a closer. */
static mote_bool bracket_kind(char ch, int *kind, int *dir) {
  const char *p;
  if (!ch) return MOTE_FALSE;
  if ((p = strchr(bracket_open, ch)) != NULL) {
    *kind = (int)(p - bracket_open);
    *dir = 1;
    return MOTE_TRUE;
  }
  if ((p = strchr(bracket_close, ch)) != NULL) {
    *kind = (int)(p - bracket_close);
    *dir = -1;
    return MOTE_TRUE;
  }
  return MOTE_FALSE;
}

/* Bracket pair under the caret, or just before it, into bracket_a/_b. */
static void find_bracket(Doc *d) {
  size_t len = buf_len(&d->buf), pos = d->caret, i;
  int kind, dir, depth = 0;
  char ch;
  d->bracket_a = d->bracket_b = NO_POS;
  if (len == 0) return;
  if (pos >= len) pos = len - 1;
  if (!bracket_kind(buf_at(&d->buf, pos), &kind, &dir)) {
    if (pos == 0 || !bracket_kind(buf_at(&d->buf, pos - 1), &kind, &dir)) return;
    pos--;
  }
  for (i = pos;; i = dir > 0 ? i + 1 : i - 1) {
    ch = buf_at(&d->buf, i);
    if (ch == bracket_open[kind]) depth += dir;
    else if (ch == bracket_close[kind]) depth -= dir;
    if (depth == 0) {
      d->bracket_a = pos;
      d->bracket_b = i;
      return;
    }
    if (dir > 0 ? i + 1 >= len : i == 0) return;
  }
}

static void do_replace_all(Editor *e, Doc *d) {
  size_t rlen, i = 0, count = 0, mlen;
  if (!editable(e, d)) return;
  if (!e->find[0]) {
    set_status(e, "find first (Ctrl+F)");
    return;
  }
  rlen = strlen(e->replace);
  while (i < buf_len(&d->buf)) {
    if (!match_at(e, d, i, &mlen)) {
      i = ed_next(d, i);
      continue;
    }
    if (!push_delete(e, d, i, mlen) ||
        (rlen && !push_insert(e, d, i, e->replace, rlen, MOTE_FALSE))) {
      set_status(e, "replace aborted");
      break;
    }
    count++;
    i += rlen;
  }
  place_caret(e, d, d->caret);
  set_statusf(e, "replaced %lu", (unsigned long)count);
}

/* Copy up to the next unescaped '/' into `out`; returns that '/' or NULL. */
static const char *slash_field(const char *p, char *out, size_t n) {
  const char *q = p;
  size_t len;
  while (*q && *q != '/') q += (*q == '\\' && q[1]) ? 2 : 1;
  if (*q != '/') return NULL;
  len = (size_t)(q - p);
  if (len >= n) len = n - 1;
  memcpy(out, p, len);
  out[len] = 0;
  return q;
}

/* "/pat/" or "/pat/repl/": regex find / replace typed into the prompt. */
static mote_bool parse_slash_cmd(const char *in, char *pat, size_t patn, char *repl,
                                 size_t repln) {
  const char *q;
  if (!in || in[0] != '/') return MOTE_FALSE;
  q = slash_field(in + 1, pat, patn);
  if (!q) return MOTE_FALSE;
  if (!slash_field(q + 1, repl, repln)) repl[0] = 0;
  return MOTE_TRUE;
}

static const char *comment_prefix(Doc *d) {
  const char *lang = hl_lang_name(hl_select(d->path));
  if (strcmp(lang, "python") == 0 || strcmp(lang, "shell") == 0 ||
      strcmp(lang, "yaml") == 0)
    return "#";
  return "//";
}

/* Rows covered by the selection, or just the caret row. */
static void sel_rows(Doc *d, size_t *r0, size_t *r1) {
  size_t col, hi = sel_hi(d);
  if (!has_sel(d)) {
    *r0 = *r1 = d->caret_row;
    return;
  }
  pos_to_rc(d, sel_lo(d), r0, &col);
  pos_to_rc(d, hi > 0 ? hi - 1 : hi, r1, &col);
}

/* Text of `row` after its indentation; false for blank lines. */
static mote_bool line_body(Doc *d, size_t row, size_t *pos, size_t *end) {
  size_t p = row_start(d, row);
  *end = line_end(d, p);
  while (p < *end && (buf_at(&d->buf, p) == ' ' || buf_at(&d->buf, p) == '\t')) p++;
  *pos = p;
  return p < *end;
}

/* Comments every non-blank line in range, or uncomments if all already are. */
static void toggle_comment(Editor *e, Doc *d) {
  size_t r0, r1, row, pos, end, n;
  const char *pfx;
  mote_bool any = MOTE_FALSE, all = MOTE_TRUE;
  if (!editable(e, d)) return;
  pfx = comment_prefix(d);
  n = strlen(pfx);
  sel_rows(d, &r0, &r1);
  for (row = r0; row <= r1; row++) {
    if (!line_body(d, row, &pos, &end)) continue;
    any = MOTE_TRUE;
    if (end - pos < n || !buf_match(&d->buf, pos, pfx, n)) all = MOTE_FALSE;
  }
  if (!any) return;
  for (row = r0; row <= r1; row++) {
    if (!line_body(d, row, &pos, &end)) continue;
    if (all) push_delete(e, d, pos, n);
    else push_insert(e, d, pos, pfx, n, MOTE_FALSE);
  }
  sync_caret_rc(d);
  ensure_visible(e, d);
  set_status(e, all ? "uncommented" : "commented");
}

/* Case-insensitive subsequence match: "edc" matches "editor.c". */
static mote_bool fuzzy_match(const char *q, const char *name) {
  for (; *q; q++, name++) {
    while (*name && tolower((unsigned char)*name) != tolower((unsigned char)*q)) name++;
    if (!*name) return MOTE_FALSE;
  }
  return MOTE_TRUE;
}

static void path_dir_of(const char *path, char *dir, size_t n) {
  const char *slash, *bs;
  size_t len;
  if (!dir || n == 0) return;
  if (!path || !path[0]) {
    mote_snprintf(dir, n, ".");
    return;
  }
  slash = strrchr(path, '/');
  bs = strrchr(path, '\\');
  if (bs && (!slash || bs > slash)) slash = bs;
  if (!slash) {
    mote_snprintf(dir, n, ".");
    return;
  }
  len = (size_t)(slash - path);
  if (len == 0) len = 1;
  if (len >= n) len = n - 1;
  memcpy(dir, path, len);
  dir[len] = 0;
}

static int cmp_name(const void *a, const void *b) {
  return strcmp((const char *)a, (const char *)b);
}

static void quickopen_filter(Editor *e) {
  int i, n = 0;
  for (i = 0; i < e->qf_pool_n && n < QF_MAX; i++) {
    if (!fuzzy_match(e->prompt, e->qf_pool[i])) continue;
    mote_snprintf(e->qf_match[n++], sizeof e->qf_match[0], "%s", e->qf_pool[i]);
  }
  e->qf_n = n;
  if (e->qf_sel >= n) e->qf_sel = n > 0 ? n - 1 : 0;
}

static void quickopen_begin(Editor *e, Doc *d) {
  path_dir_of(d->path[0] ? d->path : ".", e->qf_dir, sizeof e->qf_dir);
  e->qf_pool_n = dirlist_files(e->qf_dir, e->qf_pool, QF_POOL);
  qsort(e->qf_pool, (size_t)e->qf_pool_n, sizeof e->qf_pool[0], cmp_name);
  e->qf_sel = 0;
  begin_mode(e, MODE_QUICKOPEN, "Go to file — type filter  j/k Enter");
  quickopen_filter(e);
}

static void goto_line(Editor *e, Doc *d, size_t line1) {
  size_t row = line1 > 0 ? line1 - 1 : 0;
  ensure_lines(d);
  if (d->lines.n && row >= d->lines.n) row = d->lines.n - 1;
  place_caret(e, d, row_start(d, row));
  set_status(e, "ok");
}

static size_t bookmark_target_row(Doc *d) {
  size_t row, ls, le, len;
  sync_caret_rc(d);
  ensure_lines(d);
  row = d->caret_row;
  len = buf_len(&d->buf);
  ls = line_start(d, d->caret);
  le = line_end(d, ls);
  /* Ignore phantom empty line after trailing newline at EOF. */
  if (row > 0 && ls == le && ls == len) row--;
  if (d->lines.n && row >= d->lines.n) row = d->lines.n - 1;
  return row;
}

/* Slot holding a bookmark on `row`, or -1. */
static int bm_find(const Doc *d, size_t row) {
  int i;
  for (i = 0; i < MAX_BOOKMARKS; i++)
    if (d->bm_row[i] == row) return i;
  return -1;
}

static void bookmark_toggle(Editor *e, Doc *d) {
  size_t row = bookmark_target_row(d);
  int i = bm_find(d, row);
  if (i >= 0) {
    d->bm_row[i] = NO_POS;
    set_status(e, "bookmark cleared");
    return;
  }
  i = bm_find(d, NO_POS);
  if (i < 0) {
    set_statusf(e, "max %d bookmarks, F8 on one clears it", MAX_BOOKMARKS);
    return;
  }
  d->bm_row[i] = row;
  set_statusf(e, "bookmark set: line %lu", (unsigned long)(row + 1));
}

/* Closest bookmark to the caret line, ties going down. The caret's own
   line is only a target when it holds the sole bookmark. */
static void bookmark_jump(Editor *e, Doc *d) {
  size_t cur, r, dist, best = NO_POS, best_dist = 0;
  int i;
  cur = bookmark_target_row(d);
  for (i = 0; i < MAX_BOOKMARKS; i++) {
    r = d->bm_row[i];
    if (r == NO_POS || r == cur) continue;
    dist = r > cur ? r - cur : cur - r;
    if (best == NO_POS || dist < best_dist || (dist == best_dist && r > cur)) {
      best = r;
      best_dist = dist;
    }
  }
  if (best == NO_POS) {
    if (bm_find(d, cur) < 0) {
      set_status(e, "no bookmarks (F8 sets one)");
      return;
    }
    best = cur;
  }
  place_caret(e, d, row_start(d, best));
  set_statusf(e, "bookmark: line %lu", (unsigned long)(best + 1));
}

/* Enter keeps the current line's leading whitespace. */
static void insert_newline_indent(Editor *e, Doc *d) {
  size_t i = line_start(d, d->caret), n = 1;
  char ind[160];
  ind[0] = '\n';
  while (i < d->caret && n < sizeof ind) {
    char c = buf_at(&d->buf, i++);
    if (c != ' ' && c != '\t') break;
    ind[n++] = c;
  }
  insert_text(e, d, ind, n);
}

/* Leading whitespace to strip when outdenting the line at `pos`:
   one tab or up to four spaces. */
static size_t outdent_width(const Doc *d, size_t pos) {
  size_t n = 0, len = buf_len(&d->buf);
  if (pos < len && buf_at(&d->buf, pos) == '\t') return 1;
  while (n < 4 && pos + n < len && buf_at(&d->buf, pos + n) == ' ') n++;
  return n;
}

/* Shift `p` left for `n` bytes deleted at `at`. */
static size_t pos_after_delete(size_t p, size_t at, size_t n) {
  if (p <= at) return p;
  return p - (p - at < n ? p - at : n);
}

/* Tab / Shift+Tab: indent or outdent every selected line. Without a
   selection Tab just inserts a tab. */
static void indent_sel(Editor *e, Doc *d, int dir) {
  size_t a = d->caret, b = d->caret, row, r0, r1, pos, n;
  if (!editable(e, d)) return;
  if (!has_sel(d) && dir > 0) {
    insert_text(e, d, "\t", 1);
    return;
  }
  if (has_sel(d)) {
    a = sel_lo(d);
    b = sel_hi(d);
  }
  sel_rows(d, &r0, &r1);
  for (row = r0; row <= r1; row++) {
    pos = row_start(d, row);
    if (dir > 0) {
      if (!push_insert(e, d, pos, "\t", 1, MOTE_FALSE)) break;
      if (a >= pos) a++;
      b++;
      continue;
    }
    n = outdent_width(d, pos);
    if (!n) continue;
    if (!push_delete(e, d, pos, n)) break;
    a = pos_after_delete(a, pos, n);
    b = pos_after_delete(b, pos, n);
  }
  d->sel_anchor = a;
  d->caret = b;
  sync_caret_rc(d);
  ensure_visible(e, d);
  mark(e);
}

static void delete_line(Editor *e, Doc *d) {
  size_t a, b, len;
  if (!editable(e, d)) return;
  a = line_start(d, d->caret);
  b = line_end(d, d->caret);
  len = buf_len(&d->buf);
  if (b < len) b++;           /* take the line's newline */
  else if (a > 0) a--;        /* last line: take the newline before it */
  push_delete(e, d, a, b - a);
  place_caret(e, d, a);
}

/* Duplicate the caret line below itself, keeping the caret column. */
static void dup_line(Editor *e, Doc *d) {
  size_t a, b, off;
  char *s;
  mote_bool ok;
  if (!editable(e, d)) return;
  a = line_start(d, d->caret);
  b = line_end(d, d->caret);
  off = d->caret - a;
  /* "\n<line>" inserted at the line end also works on the last line */
  s = (char *)malloc(b - a + 1);
  if (!s) return;
  s[0] = '\n';
  buf_get(&d->buf, a, b - a, s + 1);
  ok = push_insert(e, d, b, s, b - a + 1, MOTE_FALSE);
  free(s);
  if (ok) place_caret(e, d, b + 1 + off);
}

static void cycle_theme(Editor *e) {
  e->theme_id = (e->theme_id + 1) % theme_count();
  set_statusf(e, "theme: %s", theme_name(e->theme_id));
}

static void copy_sel(Editor *e, Doc *d, Plat *p) {
  char *s;
  size_t a, b;
  if (!has_sel(d)) return;
  a = sel_lo(d);
  b = sel_hi(d);
  s = slice_dup(d, a, b);
  if (!s) return;
  plat_clipboard_set(p, s, b - a);
  free(s);
  set_status(e, "copied");
}

static void cut_sel(Editor *e, Doc *d, Plat *p) {
  if (!editable(e, d)) return;
  copy_sel(e, d, p);
  delete_sel(e, d);
  ensure_visible(e, d);
}

static void paste_clip(Editor *e, Doc *d, Plat *p) {
  size_t n = 0, room;
  char *s;
  if (!editable(e, d)) return;
  s = plat_clipboard_get(p, &n);
  if (!s) return;
  room = MOTE_MAX_FILE - buf_len(&d->buf);
  if (n > room) {
    n = room;
    set_status(e, room ? "paste truncated" : "file at size limit");
  }
  if (n) insert_at_caret(e, d, s, n, MOTE_FALSE);
  free(s);
}

static size_t click_to_pos(Editor *e, Doc *d, int mx, int my) {
  size_t row, col, vr, vc;
  if (e->ch <= 0 || e->cw <= 0) return d->caret;
  if (my < 0) my = 0;
  if (e->rows > 0 && my >= e->rows * e->ch) my = e->rows * e->ch - 1;
  mx -= e->gutter;
  if (mx < 0) mx = 0;
  vr = view_vrow0(e, d, NULL) + (size_t)(my / e->ch);
  vc = e->wrap ? (size_t)(mx / e->cw) : d->col0 + (size_t)(mx / e->cw);
  vis_to_pos(e, d, vr, vc, &row, &col);
  return rc_to_pos(d, row, col);
}

static void bm_clear_all(Doc *d) {
  int i;
  for (i = 0; i < MAX_BOOKMARKS; i++) d->bm_row[i] = NO_POS;
}

static void doc_reset(Doc *d) {
  memset(d, 0, sizeof *d);
  d->lines.dirty = MOTE_TRUE;
  d->bracket_a = d->bracket_b = NO_POS;
  bm_clear_all(d);
  d->eol = EOL_LF;
}

static mote_bool doc_init_empty(Doc *d) {
  doc_reset(d);
  if (!buf_init(&d->buf, 0)) return MOTE_FALSE;
  undo_init(&d->undo);
  return MOTE_TRUE;
}

static void doc_free(Doc *d) {
  buf_free(&d->buf);
  undo_free(&d->undo);
  free(d->lines.off);
  d->lines.off = NULL;
  d->lines.n = d->lines.capa = 0;
}

/* Swap in new contents and reset everything tied to the old text. */
static void doc_set_contents(Doc *d, Buf *nb, const char *path) {
  buf_free(&d->buf);
  d->buf = *nb;
  mote_snprintf(d->path, sizeof d->path, "%s", path);
  d->dirty = d->readonly = MOTE_FALSE;
  d->eol = EOL_LF;
  d->caret = d->sel_anchor = d->pref_col = 0;
  d->row0 = d->col0 = d->wrap0 = 0;
  d->caret_row = d->caret_col = 0;
  d->match_a = d->match_b = 0;
  d->bracket_a = d->bracket_b = NO_POS;
  bm_clear_all(d);
  lines_mark_dirty(d);
  undo_free(&d->undo);
  undo_init(&d->undo);
}

static mote_bool any_dirty(const Editor *e) {
  int i;
  for (i = 0; i < e->ndocs; i++)
    if (e->docs[i].dirty) return MOTE_TRUE;
  return MOTE_FALSE;
}

mote_bool ed_init(Editor *e) {
  memset(e, 0, sizeof *e);
  e->ndocs = 1;
  if (!doc_init_empty(D(e))) return MOTE_FALSE;
  e->need_draw = MOTE_TRUE;
  return MOTE_TRUE;
}

void ed_free(Editor *e) {
  int i;
  free(e->vrow_cache);
  e->vrow_cache = NULL;
  for (i = 0; i < e->ndocs; i++) doc_free(&e->docs[i]);
}

/* Opens into the current doc; a dirty one gets a new tab, or a
   save / discard question once all MAX_DOCS tabs are taken. A missing
   file opens as a new empty one. */
mote_bool ed_open_path(Editor *e, const char *path) {
  Doc *d = D(e);
  Buf nb;
  mote_bool exists = MOTE_TRUE;
  if (d->dirty) {
    if (e->ndocs >= MAX_DOCS) {
      mote_snprintf(e->pending_path, sizeof e->pending_path, "%s", path);
      unsaved_ask(e, MODE_OPENASK, "open");
      return MOTE_FALSE;
    }
    ed_new_doc(e);
    d = D(e);
  }
  if (!buf_init(&nb, 0)) {
    set_status(e, "out of memory");
    return MOTE_FALSE;
  }
  if (!buf_load(&nb, path)) {
    int err = errno;
    buf_free(&nb);
    if (err != ENOENT) {
      set_status(e, "open failed");
      return MOTE_FALSE;
    }
    if (!buf_init(&nb, 0)) {
      set_status(e, "out of memory");
      return MOTE_FALSE;
    }
    exists = MOTE_FALSE;
  }
  doc_set_contents(d, &nb, path);
  if (exists) normalize_eol(d);
  vrow_invalidate(e);
  recent_add(e, path);
  set_status(e, exists ? "opened" : "new file");
  return MOTE_TRUE;
}

static void doc_status(Editor *e) {
  set_statusf(e, "doc %d/%d", e->cur + 1, e->ndocs);
}

static void switch_doc(Editor *e, int idx) {
  if (idx < 0 || idx >= e->ndocs) return;
  e->cur = idx;
  vrow_invalidate(e);
  doc_status(e);
}

void ed_new_doc(Editor *e) {
  if (e->ndocs >= MAX_DOCS) {
    set_status(e, "max docs");
    return;
  }
  if (!doc_init_empty(&e->docs[e->ndocs])) {
    set_status(e, "out of memory");
    return;
  }
  e->cur = e->ndocs++;
  vrow_invalidate(e);
  set_status(e, "new doc");
}

/* Close the current doc without asking; the last one becomes empty. */
static void close_doc_force(Editor *e) {
  Doc *d = D(e);
  doc_free(d);
  vrow_invalidate(e);
  if (e->ndocs <= 1) {
    set_status(e, doc_init_empty(d) ? "closed" : "out of memory");
    return;
  }
  memmove(&e->docs[e->cur], &e->docs[e->cur + 1],
          (size_t)(e->ndocs - e->cur - 1) * sizeof e->docs[0]);
  e->ndocs--;
  if (e->cur >= e->ndocs) e->cur = e->ndocs - 1;
  doc_status(e);
}

static void close_doc(Editor *e) {
  if (D(e)->dirty) unsaved_ask(e, MODE_CLOSEASK, "close");
  else close_doc_force(e);
}

static void request_quit(Editor *e) {
  if (any_dirty(e)) unsaved_ask(e, MODE_QUITASK, "quit");
  else e->want_quit = MOTE_TRUE;
}

/* Save every dirty doc, then quit; an untitled one asks Save As first
   and resumes the quit from there. */
static void save_all_and_quit(Editor *e) {
  int i;
  for (i = 0; i < e->ndocs; i++) {
    Doc *d = &e->docs[i];
    if (!d->dirty) continue;
    if (!d->path[0]) {
      e->cur = i;
      e->pending = PENDING_QUIT;
      begin_mode(e, MODE_SAVEAS, "Save As:");
      return;
    }
    if (!save_to(e, d, d->path)) return;
  }
  e->want_quit = MOTE_TRUE;
}

static void drop_pending(Editor *e) {
  e->pending = PENDING_NONE;
  e->pending_path[0] = 0;
}

/* Run the quit / open / close that waited on an unsaved doc. */
static void finish_pending(Editor *e) {
  PendingAction act = e->pending;
  char path[sizeof e->pending_path];
  mote_snprintf(path, sizeof path, "%s", e->pending_path);
  drop_pending(e);
  e->mode = MODE_EDIT;
  switch (act) {
  case PENDING_QUIT: save_all_and_quit(e); break;
  case PENDING_OPEN: if (path[0]) ed_open_path(e, path); break;
  case PENDING_CLOSE: close_doc_force(e); break;
  default: break;
  }
}

/* The "Unsaved — ^S save  ^Q discard  Esc" question. */
static void handle_ask(Editor *e, Doc *d, const PlatEvent *ev) {
  PendingAction act = e->mode == MODE_QUITASK   ? PENDING_QUIT
                      : e->mode == MODE_OPENASK ? PENDING_OPEN
                                                : PENDING_CLOSE;
  if (ev->type != PE_KEY) return;
  switch (ev->key) {
  case PK_QUIT:
    if (act == PENDING_QUIT) {
      e->want_quit = MOTE_TRUE;
      return;
    }
    d->dirty = MOTE_FALSE;
    e->pending = act;
    finish_pending(e);
    break;
  case PK_SAVE:
    if (act == PENDING_QUIT) {
      save_all_and_quit(e);
    } else if (!d->path[0]) {
      e->pending = act;
      begin_mode(e, MODE_SAVEAS, "Save As:");
    } else if (save_to(e, d, d->path)) {
      e->pending = act;
      finish_pending(e);
    }
    break;
  case PK_ESCAPE:
    drop_pending(e);
    leave_mode(e);
    break;
  default:
    break;
  }
}

static void jump_bracket(Editor *e, Doc *d) {
  find_bracket(d);
  if (d->bracket_a == NO_POS) {
    set_status(e, "no match");
    return;
  }
  move_caret(e, d, d->caret == d->bracket_b ? d->bracket_a : d->bracket_b, MOTE_FALSE);
}

static void reload_doc(Editor *e, Doc *d) {
  if (!d->path[0]) {
    set_status(e, "no path");
    return;
  }
  if (d->dirty) {
    set_status(e, "save first");
    return;
  }
  if (ed_open_path(e, d->path)) set_status(e, "reloaded");
}

static void toggle_eol(Editor *e, Doc *d) {
  d->eol = d->eol == EOL_LF ? EOL_CRLF : EOL_LF;
  d->dirty = MOTE_TRUE;
  set_status(e, d->eol == EOL_CRLF ? "eol CRLF" : "eol LF");
}

static void toggle_wrap(Editor *e, Doc *d) {
  toggle_flag(e, &e->wrap, "wrap");
  vrow_invalidate(e);
  if (e->wrap) d->col0 = d->wrap0 = 0;
  ensure_visible(e, d);
}

static void zoom(Editor *e, Plat *p, int delta) {
  plat_set_font_px(p, delta ? plat_font_px(p) + delta : MOTE_FONT_PX);
  set_status(e, delta > 0 ? "zoom+" : delta < 0 ? "zoom-" : "zoom reset");
}

static void select_all(Editor *e, Doc *d) {
  d->sel_anchor = 0;
  d->caret = buf_len(&d->buf);
  sync_caret_rc(d);
  mark(e);
}

static void delete_back(Editor *e, Doc *d) {
  size_t np;
  if (!editable(e, d)) return;
  if (has_sel(d)) {
    delete_sel(e, d);
  } else if (d->caret > 0) {
    np = ed_prev(d, d->caret);
    if (push_delete(e, d, np, d->caret - np)) d->caret = np;
  }
  place_caret(e, d, d->caret);
}

static void delete_fwd(Editor *e, Doc *d) {
  if (!editable(e, d)) return;
  if (has_sel(d)) delete_sel(e, d);
  else if (d->caret < buf_len(&d->buf))
    push_delete(e, d, d->caret, ed_next(d, d->caret) - d->caret);
  place_caret(e, d, d->caret);
}

/* --- prompts (Open, Save As, Find, Replace, Goto) --- */

static mote_bool prompt_append(Editor *e, const char *s, int n) {
  size_t len = strlen(e->prompt);
  if (n <= 0 || len + (size_t)n >= sizeof e->prompt - 1) return MOTE_FALSE;
  memcpy(e->prompt + len, s, (size_t)n);
  e->prompt[len + (size_t)n] = 0;
  mark(e);
  return MOTE_TRUE;
}

static mote_bool prompt_backspace(Editor *e) {
  size_t n = strlen(e->prompt);
  if (!n) return MOTE_FALSE;
  e->prompt[utf8_prev(e->prompt, n)] = 0;
  mark(e);
  return MOTE_TRUE;
}

static void find_begin(Editor *e, Doc *d) {
  begin_mode(e, MODE_FIND, "Find: /re/ or text  Alt+C case  Alt+W word");
  mote_snprintf(e->prompt, sizeof e->prompt, e->find_regex ? "/%s/" : "%s", e->find);
  d->match_a = d->match_b = 0;
}

/* Find prompt -> search pattern: "/re/" is a regex, anything else text. */
static void find_from_prompt(Editor *e) {
  char pat[sizeof e->find], repl[sizeof e->replace];
  e->find_regex = parse_slash_cmd(e->prompt, pat, sizeof pat, repl, sizeof repl);
  mote_snprintf(e->find, sizeof e->find, "%s", e->find_regex ? pat : e->prompt);
}

/* Replace prompt: "/find/repl/" sets both (regex), plain text is the
   replacement for the current find pattern. */
static void replace_from_prompt(Editor *e) {
  char pat[sizeof e->find], repl[sizeof e->replace];
  if (parse_slash_cmd(e->prompt, pat, sizeof pat, repl, sizeof repl)) {
    mote_snprintf(e->find, sizeof e->find, "%s", pat);
    mote_snprintf(e->replace, sizeof e->replace, "%s", repl);
    e->find_regex = MOTE_TRUE;
  } else {
    mote_snprintf(e->replace, sizeof e->replace, "%s", e->prompt);
  }
}

static void prompt_enter(Editor *e, Doc *d) {
  EdMode mode = e->mode;
  e->mode = MODE_EDIT;
  switch (mode) {
  case MODE_OPEN:
    if (e->prompt[0]) ed_open_path(e, e->prompt);
    break;
  case MODE_SAVEAS:
    if (e->prompt[0] && save_to(e, d, e->prompt)) finish_pending(e);
    else drop_pending(e);
    break;
  case MODE_FIND:
    find_from_prompt(e);
    find_next(e, d);
    break;
  case MODE_REPLACE:
    replace_from_prompt(e);
    do_replace_all(e, d);
    break;
  case MODE_GOTO:
    goto_line(e, d, (size_t)strtoul(e->prompt, NULL, 10));
    break;
  default:
    break;
  }
  mark(e);
}

static void handle_prompt(Editor *e, Doc *d, const PlatEvent *ev) {
  if (ev->type == PE_KEY) {
    if (ev->key == PK_ESCAPE) {
      drop_pending(e);
      leave_mode(e);
    } else if (ev->key == PK_ENTER) {
      prompt_enter(e, d);
    } else if (ev->key == PK_BACKSPACE) {
      prompt_backspace(e);
    }
    return;
  }
  if (ev->type != PE_TEXT || ev->text_len <= 0) return;
  /* '/' typed into a plain-text find starts over as a regex */
  if ((e->mode == MODE_FIND || e->mode == MODE_REPLACE) && ev->text[0] == '/' &&
      e->prompt[0] && e->prompt[0] != '/')
    e->prompt[0] = 0;
  prompt_append(e, ev->text, ev->text_len);
}

/* Keys that work both while editing and inside a prompt. */
static mote_bool handle_global_keys(Editor *e, Doc *d, const PlatEvent *ev) {
  if (ev->type != PE_KEY) return MOTE_FALSE;
  switch (ev->key) {
  case PK_BOOKMARK_SET: bookmark_toggle(e, d); break;
  case PK_BOOKMARK: bookmark_jump(e, d); break;
  case PK_FINDCASE: toggle_flag(e, &e->find_case, "find: case"); break;
  case PK_FINDWORD: toggle_flag(e, &e->find_word, "find: word"); break;
  case PK_FINDNEXT:
  case PK_FINDPREV:
    if (e->mode == MODE_FIND) find_from_prompt(e);
    if (!e->find[0]) set_status(e, "type pattern, Enter");
    else if (ev->key == PK_FINDNEXT) find_next(e, d);
    else find_prev(e, d);
    break;
  default:
    return MOTE_FALSE;
  }
  return MOTE_TRUE;
}

/* --- popups (help, recent files, quick open) --- */

static void handle_help(Editor *e, const PlatEvent *ev) {
  if (ev->type != PE_KEY) return; /* focus noise / stray text must not close it */
  switch (ev->key) {
  case PK_ESCAPE:
  case PK_F1:
  case PK_HELP:
  case PK_ENTER:
    leave_mode(e);
    return;
  case PK_UP: e->help_top -= 1; break;
  case PK_PGUP: e->help_top -= 10; break;
  case PK_DOWN: e->help_top += 1; break; /* upper bound clamped in ed_draw */
  case PK_PGDN: e->help_top += 10; break;
  default: return;
  }
  if (e->help_top < 0) e->help_top = 0;
  mark(e);
}

/* Up/Down or j/k move a list selection; true if `ev` was one of them. */
static mote_bool list_nav(Editor *e, const PlatEvent *ev, int *sel, int n) {
  int dy;
  char c = ev->type == PE_TEXT && ev->text_len == 1 ? ev->text[0] : 0;
  if ((ev->type == PE_KEY && ev->key == PK_UP) || c == 'k') dy = -1;
  else if ((ev->type == PE_KEY && ev->key == PK_DOWN) || c == 'j') dy = 1;
  else return MOTE_FALSE;
  *sel += dy;
  if (*sel >= n) *sel = n - 1;
  if (*sel < 0) *sel = 0;
  mark(e);
  return MOTE_TRUE;
}

static void recent_begin(Editor *e) {
  if (!e->nrecent) {
    set_status(e, "no recent");
    return;
  }
  e->recent_sel = 0;
  begin_mode(e, MODE_RECENT, "Recent — j/k Enter, 1-8");
}

static void handle_recent(Editor *e, const PlatEvent *ev) {
  char path[sizeof e->recent[0]];
  int pick;
  if (list_nav(e, ev, &e->recent_sel, e->nrecent)) return;
  if (ev->type == PE_KEY && ev->key == PK_ESCAPE) {
    leave_mode(e);
    return;
  }
  if (ev->type == PE_KEY && ev->key == PK_ENTER)
    pick = e->recent_sel;
  else if (ev->type == PE_TEXT && ev->text_len == 1 && ev->text[0] >= '1' &&
           ev->text[0] <= '0' + MAX_RECENT)
    pick = ev->text[0] - '1';
  else
    return;
  if (pick >= e->nrecent) return;
  mote_snprintf(path, sizeof path, "%s", e->recent[pick]);
  e->mode = MODE_EDIT;
  ed_open_path(e, path);
}

static void handle_quickopen(Editor *e, const PlatEvent *ev) {
  char path[sizeof e->qf_dir + sizeof e->qf_match[0]];
  if (list_nav(e, ev, &e->qf_sel, e->qf_n)) return;
  if (ev->type == PE_TEXT) {
    if (prompt_append(e, ev->text, ev->text_len)) quickopen_filter(e);
    return;
  }
  if (ev->type != PE_KEY) return;
  if (ev->key == PK_BACKSPACE) {
    if (prompt_backspace(e)) quickopen_filter(e);
  } else if (ev->key == PK_ESCAPE) {
    leave_mode(e);
  } else if (ev->key == PK_ENTER) {
    e->mode = MODE_EDIT;
    if (e->qf_n > 0) {
      mote_snprintf(path, sizeof path, "%s/%s", e->qf_dir, e->qf_match[e->qf_sel]);
      ed_open_path(e, path);
    }
  }
}

/* --- edit mode --- */

static void handle_mouse(Editor *e, Doc *d, const PlatEvent *ev) {
  size_t top;
  switch (ev->type) {
  case PE_SCROLL:
    top = view_vrow0(e, d, NULL);
    if (ev->wheel > 0) top = top > (size_t)ev->wheel ? top - (size_t)ev->wheel : 0;
    else top += (size_t)(-ev->wheel);
    set_view_vrow(e, d, top);
    mark(e);
    break;
  case PE_MOUSE_DOWN:
    e->mouse_down = MOTE_TRUE;
    move_caret(e, d, click_to_pos(e, d, ev->mx, ev->my), ev->shift);
    break;
  case PE_MOUSE_MOVE:
    if (e->mouse_down) move_caret(e, d, click_to_pos(e, d, ev->mx, ev->my), MOTE_TRUE);
    break;
  case PE_MOUSE_UP:
    e->mouse_down = MOTE_FALSE;
    break;
  default:
    break;
  }
}

static void handle_edit_text(Editor *e, Doc *d, const PlatEvent *ev) {
  static const char opens[] = "([{\"'";
  static const char closes[] = ")]}\"'";
  const char *q = NULL;
  if (ev->text_len == 1 && ev->text[0]) q = strchr(opens, ev->text[0]);
  if (q) insert_autoclose(e, d, *q, closes[q - opens]);
  else insert_text(e, d, ev->text, (size_t)ev->text_len);
}

static void handle_edit_key(Editor *e, Doc *d, Plat *p, const PlatEvent *ev) {
  mote_bool keep = ev->shift;
  int page = e->rows > 1 ? e->rows - 1 : 1;
  switch (ev->key) {
  case PK_LEFT:
    move_caret(e, d, ev->ctrl ? prev_word(d, d->caret) : ed_prev(d, d->caret), keep);
    break;
  case PK_RIGHT:
    move_caret(e, d, ev->ctrl ? next_word(d, d->caret) : ed_next(d, d->caret), keep);
    break;
  case PK_UP: move_vert(e, d, -1, keep); break;
  case PK_DOWN: move_vert(e, d, 1, keep); break;
  case PK_PGUP: move_vert(e, d, -page, keep); break;
  case PK_PGDN: move_vert(e, d, page, keep); break;
  case PK_HOME: move_caret(e, d, line_start(d, d->caret), keep); break;
  case PK_END: move_caret(e, d, line_end(d, d->caret), keep); break;
  case PK_BACKSPACE: delete_back(e, d); break;
  case PK_DELETE: delete_fwd(e, d); break;
  case PK_ENTER: insert_newline_indent(e, d); break;
  case PK_TAB: indent_sel(e, d, ev->shift ? -1 : 1); break;
  case PK_UNDO: do_undo(e, d, MOTE_FALSE); break;
  case PK_REDO: do_undo(e, d, MOTE_TRUE); break;
  case PK_CUT: cut_sel(e, d, p); break;
  case PK_COPY: copy_sel(e, d, p); break;
  case PK_PASTE: paste_clip(e, d, p); break;
  case PK_SELALL: select_all(e, d); break;
  case PK_DELLINE: delete_line(e, d); break;
  case PK_DUPLINE: dup_line(e, d); break;
  case PK_COMMENT: toggle_comment(e, d); break;
  case PK_BRACKET: jump_bracket(e, d); break;

  case PK_SAVE: try_save(e, d); break;
  case PK_SAVEAS: begin_mode(e, MODE_SAVEAS, "Save As:"); break;
  case PK_OPEN: begin_mode(e, MODE_OPEN, "Open:"); break;
  case PK_QUICKOPEN: quickopen_begin(e, d); break;
  case PK_RECENT: recent_begin(e); break;
  case PK_RELOAD: reload_doc(e, d); break;
  case PK_NEWDOC: ed_new_doc(e); break;
  case PK_NEXTDOC: switch_doc(e, (e->cur + 1) % e->ndocs); break;
  case PK_PREVDOC: switch_doc(e, (e->cur + e->ndocs - 1) % e->ndocs); break;
  case PK_CLOSEDOC: close_doc(e); break;
  case PK_QUIT: request_quit(e); break;

  case PK_FIND: find_begin(e, d); break;
  case PK_REPLACE: begin_mode(e, MODE_REPLACE, "Replace: /find/repl/ or text"); break;
  case PK_GOTO: begin_mode(e, MODE_GOTO, "Goto:"); break;

  case PK_HELP:
  case PK_F1:
    e->mode = MODE_HELP;
    e->help_top = 0;
    mark(e);
    break;
  case PK_THEME: cycle_theme(e); break;
  case PK_WRAP: toggle_wrap(e, d); break;
  case PK_WS: toggle_flag(e, &e->show_ws, "whitespace"); break;
  case PK_READONLY: toggle_flag(e, &d->readonly, "readonly"); break;
  case PK_EOL: toggle_eol(e, d); break;
  case PK_ZOOMIN: zoom(e, p, 1); break;
  case PK_ZOOMOUT: zoom(e, p, -1); break;
  case PK_ZOOMRESET: zoom(e, p, 0); break;
  default: break;
  }
}

void ed_handle(Editor *e, Plat *p, const PlatEvent *ev) {
  Doc *d = D(e);
  if (ev->type == PE_EXPOSE) {
    mark(e);
    return;
  }
  if (ev->type == PE_QUIT) {
    request_quit(e);
    return;
  }
  switch (e->mode) {
  case MODE_HELP: handle_help(e, ev); return;
  case MODE_RECENT: handle_recent(e, ev); return;
  case MODE_QUICKOPEN: handle_quickopen(e, ev); return;
  case MODE_QUITASK:
  case MODE_OPENASK:
  case MODE_CLOSEASK: handle_ask(e, d, ev); return;
  default: break;
  }
  if (handle_global_keys(e, d, ev)) return;
  if (e->mode != MODE_EDIT) handle_prompt(e, d, ev);
  else if (ev->type == PE_KEY) handle_edit_key(e, d, p, ev);
  else if (ev->type == PE_TEXT && ev->text_len > 0 && !ev->ctrl) handle_edit_text(e, d, ev);
  else handle_mouse(e, d, ev);
}

static void draw_text_fit(Plat *p, int x, int y, const char *s, int n, mote_u32 rgb,
                          int max_px, int cw) {
  int max_cols, cols, i, out_n, len;
  mote_u32 cp;
  char tmp[384];
  if (!s || max_px < 1 || cw < 1) return;
  if (n < 0) n = (int)strlen(s);
  max_cols = max_px / cw;
  if (max_cols < 1) return;
  cols = 0;
  i = 0;
  out_n = 0;
  while (i < n && cols < max_cols) {
    len = utf8_decode(s + i, (size_t)(n - i), &cp);
    if (len <= 0) {
      i++;
      continue;
    }
    if (out_n + len > (int)sizeof tmp) break;
    memcpy(tmp + out_n, s + i, (size_t)len);
    out_n += len;
    cols++;
    i += len;
  }
  if (out_n > 0) plat_draw_text(p, x, y, tmp, out_n, rgb);
}

typedef struct {
  const char *key, *desc; /* desc == NULL: section header */
} HelpItem;

static const HelpItem help_left[] = {
    {"FILE", NULL},
    {"^S / Alt+S", "save / save as"},
    {"^O / ^P", "open / quick open"},
    {"^N", "new file"},
    {"^E", "recent files"},
    {"F5", "reload from disk"},
    {"^Tab / F2", "next file (+Sh: prev)"},
    {"^F4 / ^Sh+W", "close file"},
    {"^Q", "quit"},
    {"FIND", NULL},
    {"^F", "find, /re/ = regex"},
    {"F3 / Sh+F3", "next / previous match"},
    {"^R", "replace, /a/b/ = regex"},
    {"^G", "go to line"},
    {"Alt+C / W", "match case / whole word"},
    {"BOOKMARKS", NULL},
    {"F8 / Alt+M", "toggle on this line"},
    {"F9 / Alt+J", "jump to nearest"},
};

static const HelpItem help_right[] = {
    {"EDIT", NULL},
    {"^Z / ^Y", "undo / redo"},
    {"^X ^C ^V", "cut / copy / paste"},
    {"^A", "select all"},
    {"^D", "duplicate line"},
    {"Alt+K", "delete line"},
    {"^/", "comment line"},
    {"(Sh+)Tab", "indent / outdent"},
    {"^]", "jump to bracket"},
    {"VIEW", NULL},
    {"^W", "word wrap"},
    {"F7", "show whitespace"},
    {"^T", "next theme"},
    {"^= ^- ^0", "zoom in / out / reset"},
    {"Alt+R", "read-only"},
    {"Alt+E", "LF / CRLF line ends"},
    {"F1 / Esc", "close this help"},
    {"^ = Ctrl", "Sh = Shift"},
};

#define HELP_NL ((int)(sizeof help_left / sizeof help_left[0]))
#define HELP_NR ((int)(sizeof help_right / sizeof help_right[0]))
#define HELP_KEY_W 13    /* left key column, cells */
#define HELP_COL2 38     /* right column start, cells */
#define HELP_KEY2_W 10   /* right key column, cells */
#define HELP_TWO_COL_W 70 /* min text width for two columns, cells */

static void draw_help_item(Plat *p, const Theme *t, const HelpItem *it, int x, int y,
                           int key_w, int max_px, int cw) {
  if (!it->desc) {
    draw_text_fit(p, x, y, it->key, -1, t->type, max_px, cw);
    return;
  }
  draw_text_fit(p, x, y, it->key, -1, t->kw, max_px, cw);
  if (max_px > key_w * cw)
    draw_text_fit(p, x + key_w * cw, y, it->desc, -1, t->fg, max_px - key_w * cw, cw);
}

static void draw_range(Editor *e, Doc *d, Plat *p, size_t a, size_t b, int y,
                       size_t col0, size_t col_max, const HlSpan *spans,
                       int nspans, const Theme *t) {
  size_t i, col = 0;
  size_t slo = sel_lo(d), shi = sel_hi(d);
  mote_bool selecting = has_sel(d);
  int gx = e->gutter;

  for (i = a; i < b;) {
    mote_u32 cp;
    char chunk[4], chs[4];
    int n, wcols = 1, enc, k;
    size_t rem = b - i;
    size_t off = i - a;
    HlKind hk;
    mote_u32 fg;
    for (k = 0; k < 4 && (size_t)k < rem; k++)
      chunk[k] = buf_at(&d->buf, i + (size_t)k);
    n = utf8_decode(chunk, rem < 4 ? rem : 4, &cp);
    if (n <= 0) {
      n = 1;
      cp = '?';
    }
    if (cp == '\t') wcols = tab_cols(col);
    if (col + (size_t)wcols > col0 && col < col_max) {
      int x = gx + (int)(col - col0) * e->cw;
      int fill_w = wcols;
      if (col + (size_t)wcols > col_max) {
        fill_w = (int)(col_max - col);
        if (fill_w < 1) fill_w = 1;
      }
      if (selecting && i >= slo && i < shi)
        plat_fill_rect(p, x, y, e->cw * fill_w, e->ch, t->sel);
      else if (d->match_b > d->match_a && i >= d->match_a && i < d->match_b)
        plat_fill_rect(p, x, y, e->cw * fill_w, e->ch, t->match);
      hk = hl_kind_at(spans, nspans, off);
      if (i == d->bracket_a || i == d->bracket_b) hk = HL_BRACKET;
      fg = hl_color(t, hk);
      if (e->show_ws && (cp == ' ' || cp == '\t')) {
        /* Cell consoles: ASCII only — U+00B7/» break VT width and thrash redraw. */
        char gch = (cp == ' ') ? '.' : '>';
        if (e->cw > 1) {
          const char *glyph = (cp == ' ') ? "\xC2\xB7" : "\xC2\xBB"; /* · » */
          plat_draw_text(p, x, y, glyph, 2, t->gutter_fg);
        } else {
          plat_draw_text(p, x, y, &gch, 1, t->gutter_fg);
        }
      } else if (cp != '\t') {
        enc = utf8_encode(cp, chs);
        plat_draw_text(p, x, y, chs, enc, fg);
      }
    }
    i += (size_t)n;
    col += (size_t)wcols;
    if (col >= col_max + 8) break;
  }
}

/* Buffer bytes [a, b) as a NUL-terminated string, truncated to fit. */
static int copy_line(const Doc *d, size_t a, size_t b, char *out, int cap) {
  int i, n = (int)(b - a);
  if (n > cap - 1) n = cap - 1;
  for (i = 0; i < n; i++) out[i] = buf_at(&d->buf, a + (size_t)i);
  out[n] = 0;
  return n;
}

/* Cell size, gutter and text area for this frame; the bottom cell row is
   the status bar. */
static void layout(Editor *e, Plat *p, Doc *d, int w, int h, int *digits) {
  size_t n = d->lines.n ? d->lines.n : 1;
  e->cw = plat_font_w(p);
  e->ch = plat_font_h(p);
  if (e->cw < 1) e->cw = 8;
  if (e->ch < 1) e->ch = 16;
  for (*digits = 1; n >= 10; n /= 10) (*digits)++;
  e->gutter = (*digits + 1) * e->cw;
  if (e->gutter > w / 3) e->gutter = w / 3;
  e->cols = (w - e->gutter) / e->cw;
  e->rows = (h - e->ch) / e->ch;
  if (e->cols < 1) e->cols = 1;
  if (e->rows < 1) e->rows = 1;
  if (e->wrap) {
    size_t segs = segs_of(e, d, d->row0);
    d->col0 = 0;
    if (d->wrap0 >= segs) d->wrap0 = segs - 1;
  }
}

/* Wrap mode: cached first visual row of every line; NULL without wrap. */
static const size_t *vrow_table(Editor *e, Doc *d) {
  size_t n = d->lines.n ? d->lines.n : 1;
  if (!e->wrap) return NULL;
  if (!e->vrow_cache || e->vrow_n != n || e->vrow_cols != e->cols) {
    free(e->vrow_cache);
    e->vrow_cache = build_vrow_prefix(e, d, n);
    e->vrow_n = e->vrow_cache ? n : 0;
    e->vrow_cols = e->cols;
  }
  return e->vrow_cache;
}

#define HL_RESCAN_ROWS 128 /* how far above the view a cold rescan starts */

/* Block-comment state at the top visible row: resumed from the previous
   frame when scrolling down, else rescanned from HL_RESCAN_ROWS above. */
static int hl_state_at_view(Doc *d, const HlSyntax *syn) {
  size_t r, a;
  int in_ml = 0, n;
  char line[4096];
  if (!syn || !hl_has_multiline(syn)) return 0;
  if (d->hl_ml_valid && d->hl_ml_row <= d->row0) {
    r = d->hl_ml_row;
    in_ml = d->hl_in_ml;
  } else {
    r = d->row0 > HL_RESCAN_ROWS ? d->row0 - HL_RESCAN_ROWS : 0;
  }
  for (; r < d->row0; r++) {
    a = row_start(d, r);
    n = copy_line(d, a, line_end(d, a), line, (int)sizeof line);
    hl_line(syn, line, (size_t)n, in_ml, NULL, 0, &in_ml);
  }
  d->hl_in_ml = in_ml;
  d->hl_ml_row = d->row0;
  d->hl_ml_valid = MOTE_TRUE;
  return in_ml;
}

/* Right-aligned line number with a '*' bookmark marker. */
static void draw_line_number(Editor *e, Doc *d, Plat *p, const Theme *t, size_t row,
                             int y, int digits) {
  char num[32];
  int n, x;
  mote_snprintf(num, sizeof num, "%*lu%c", digits, (unsigned long)(row + 1),
                bm_find(d, row) >= 0 ? '*' : ' ');
  n = (int)strlen(num);
  x = e->gutter - (n + 1) * e->cw; /* one cell gap before the text */
  plat_draw_text(p, x < 0 ? 0 : x, y, num, n, t->gutter_fg);
}

static void draw_text_rows(Editor *e, Doc *d, Plat *p, const Theme *t,
                           const HlSyntax *syn, int in_ml, int digits, int w) {
  size_t nlines = d->lines.n ? d->lines.n : 1;
  size_t row = d->row0, seg = d->wrap0, cols = (size_t)e->cols;
  int i = 0;
  while (i < e->rows && row < nlines) {
    HlSpan spans[HL_MAX_SPANS];
    char line[4096];
    int nspans = 0, n, y = i * e->ch;
    size_t a = row_start(d, row), b = line_end(d, a);
    size_t segs = segs_of(e, d, row);
    size_t col0 = e->wrap ? seg * cols : d->col0;
    if (seg >= segs) {
      seg = 0;
      row++;
      continue;
    }
    if (row == d->caret_row)
      plat_fill_rect(p, e->gutter, y, w - e->gutter, e->ch, t->line);
    if (seg == 0) draw_line_number(e, d, p, t, row, y, digits);
    if (syn) {
      int ml = in_ml;
      n = copy_line(d, a, b, line, (int)sizeof line);
      nspans = hl_line(syn, line, (size_t)n, ml, spans, HL_MAX_SPANS, &ml);
      if (seg + 1 >= segs) in_ml = ml;
    }
    draw_range(e, d, p, a, b, y, col0, col0 + cols, spans, nspans, t);
    i++;
    if (++seg >= segs) {
      seg = 0;
      row++;
    }
  }
}

static mote_bool popup_open(const Editor *e) {
  return e->mode == MODE_HELP || e->mode == MODE_RECENT || e->mode == MODE_QUICKOPEN;
}

static void draw_caret(Editor *e, Doc *d, Plat *p, const Theme *t, const size_t *vp) {
  size_t vr, vc, top = view_vrow0(e, d, vp);
  int x, y;
  caret_vis(e, d, &vr, &vc, vp);
  if (!e->wrap) {
    if (d->caret_col < d->col0 || d->caret_col >= d->col0 + (size_t)e->cols) vr = NO_POS;
    else vc = d->caret_col - d->col0;
  }
  if (popup_open(e) || vr == NO_POS || vr < top || vr >= top + (size_t)e->rows) {
    plat_set_caret(p, 0, 0, e->ch, MOTE_FALSE);
    return;
  }
  x = e->gutter + (int)vc * e->cw;
  y = (int)(vr - top) * e->ch;
  if (!plat_set_caret(p, x, y, e->ch, MOTE_TRUE))
    plat_fill_rect(p, x, y, e->cw, e->ch, t->caret);
}

static void draw_status(Editor *e, Doc *d, Plat *p, const Theme *t,
                        const HlSyntax *syn, int w, int h, int y) {
  char bar[384];
  const char *pfx = prompt_bar_prefix(e->mode);
  int pad = w > e->cw * 2 ? e->cw : 0;
  plat_fill_rect(p, 0, y, w, h - y, t->status);
  if (pfx[0])
    mote_snprintf(bar, sizeof bar, "%s%s%s", pfx, e->prompt[0] ? " " : "", e->prompt);
  else if (e->mode == MODE_QUITASK || e->mode == MODE_OPENASK || e->mode == MODE_CLOSEASK)
    mote_snprintf(bar, sizeof bar, "%s", e->status);
  else
    mote_snprintf(bar, sizeof bar, "[%d/%d] %s%s%s  %lu:%lu  %s  %s  %s", e->cur + 1,
                  e->ndocs, path_base(d->path[0] ? d->path : "[untitled]"),
                  d->dirty ? "*" : "", d->readonly ? " RO" : "",
                  (unsigned long)(d->caret_row + 1), (unsigned long)(d->caret_col + 1),
                  d->eol == EOL_CRLF ? "CRLF" : "LF", hl_lang_name(syn),
                  e->status[0] ? e->status : "F1 help");
  draw_text_fit(p, pad, y, bar, -1, t->status_fg, w - pad, e->cw);
}

/* Centered popup box (help, recent, quick open) above the status bar. */
typedef struct {
  int x, y, w, h; /* box, without the border */
  int inset;      /* border width */
  int tx, text_w; /* text column and width inside the box */
  int max_h;      /* tallest box that keeps the status bar visible */
} Popup;

static void popup_layout(const Editor *e, int w, int status_y, Popup *b) {
  int pad = e->cw;
  b->inset = pad > 1 ? pad / 2 : 1;
  b->x = pad * 2;
  if (b->x * 2 >= w) b->x = pad;
  b->tx = b->x + pad;
  if (b->tx >= w - pad) b->tx = b->x;
  b->y = pad * 2 > e->ch ? pad * 2 : e->ch;
  b->w = w - b->x * 2;
  if (b->w < pad * 4) b->w = w > 2 ? w - 2 : w;
  if (b->w < 1) b->w = 1;
  b->text_w = b->w - (b->tx - b->x) - pad;
  if (b->text_w < e->cw) b->text_w = b->w - (b->tx - b->x);
  if (b->text_w < 1) b->text_w = 1;
  b->max_h = status_y - b->y - b->inset;
  if (b->max_h < e->ch) b->max_h = e->ch;
  b->h = b->max_h;
}

/* Size the box for `want` content rows plus a blank row above and below;
   returns how many content rows fit. */
static int popup_fit(const Editor *e, Popup *b, int want) {
  int n = want, max_rows = b->max_h / e->ch - 2;
  if (max_rows < 1) max_rows = 1;
  if (n > max_rows) n = max_rows;
  b->h = (n + 2) * e->ch;
  if (b->h > b->max_h) {
    b->h = b->max_h - b->max_h % e->ch;
    if (b->h < e->ch) b->h = e->ch;
    n = b->h / e->ch - 2;
    if (n < 1) n = 1;
  }
  return n;
}

static void popup_frame(Plat *p, const Theme *t, const Popup *b) {
  int bx = b->x > b->inset ? b->x - b->inset : 0;
  int by = b->y > b->inset ? b->y - b->inset : 0;
  plat_fill_rect(p, bx, by, b->w + b->inset * 2, b->h + b->inset * 2, t->help_bd);
  plat_fill_rect(p, b->x, b->y, b->w, b->h, t->help_bg);
}

/* Y of content row `i` (0-based), or -1 if it falls outside the box. */
static int popup_row_y(const Editor *e, const Popup *b, int i) {
  int y = b->y + (i + 1) * e->ch;
  return y + e->ch <= b->y + b->h ? y : -1;
}

/* Two key/action columns when wide enough, else one; scrolls by help_top. */
static void draw_help(Editor *e, Plat *p, const Theme *t, Popup *b) {
  mote_bool two = b->text_w / e->cw >= HELP_TWO_COL_W;
  int total = two ? (HELP_NL > HELP_NR ? HELP_NL : HELP_NR) : HELP_NL + HELP_NR;
  int n = popup_fit(e, b, total), i, r, y;
  popup_frame(p, t, b);
  if (e->help_top > total - n) e->help_top = total - n;
  if (e->help_top < 0) e->help_top = 0;
  for (i = 0; i < n && (y = popup_row_y(e, b, i)) >= 0; i++) {
    r = i + e->help_top;
    if (!two) {
      if (r < HELP_NL + HELP_NR)
        draw_help_item(p, t, r < HELP_NL ? &help_left[r] : &help_right[r - HELP_NL],
                       b->tx, y, HELP_KEY_W, b->text_w, e->cw);
      continue;
    }
    if (r < HELP_NL)
      draw_help_item(p, t, &help_left[r], b->tx, y, HELP_KEY_W,
                     (HELP_COL2 - 1) * e->cw, e->cw);
    if (r < HELP_NR)
      draw_help_item(p, t, &help_right[r], b->tx + HELP_COL2 * e->cw, y, HELP_KEY2_W,
                     b->text_w - HELP_COL2 * e->cw, e->cw);
  }
  if (n < total && (y = popup_row_y(e, b, n)) >= 0)
    draw_text_fit(p, b->tx, y,
                  e->help_top + n < total ? "  ... Down/PgDn" : "  ... Up/PgUp", -1,
                  t->gutter_fg, b->text_w, e->cw);
}

/* Recent files or quick-open matches: a title row, then the list,
   scrolled so the selection stays visible. */
static void draw_list_popup(Editor *e, Plat *p, const Theme *t, Popup *b) {
  mote_bool recent = e->mode == MODE_RECENT;
  int count = recent ? e->nrecent : e->qf_n;
  int sel = recent ? e->recent_sel : e->qf_sel;
  int vis = popup_fit(e, b, count + 1) - 1, first, i, y;
  char line[300];
  popup_frame(p, t, b);
  draw_text_fit(p, b->tx, b->y + e->ch, recent ? "Recent" : "Go to file", -1, t->fg,
                b->text_w, e->cw);
  first = vis > 0 && sel >= vis ? sel - vis + 1 : 0;
  for (i = first; i < count && i - first < vis; i++) {
    if ((y = popup_row_y(e, b, i - first + 1)) < 0) break;
    if (recent)
      mote_snprintf(line, sizeof line, "%s%d %s", i == sel ? "> " : "  ", i + 1,
                    path_base(e->recent[i]));
    else
      mote_snprintf(line, sizeof line, "%s%s", i == sel ? "> " : "  ", e->qf_match[i]);
    draw_text_fit(p, b->tx, y, line, -1, i == sel ? t->kw : t->fg, b->text_w, e->cw);
  }
}

static void update_title(Editor *e, Plat *p, const Doc *d) {
  char title[sizeof e->title];
  mote_snprintf(title, sizeof title, "%s%s — mote", d->dirty ? "*" : "",
                path_base(d->path[0] ? d->path : "untitled"));
  if (strcmp(title, e->title) == 0) return;
  memcpy(e->title, title, sizeof title);
  plat_set_title(p, title);
}

void ed_draw(Editor *e, Plat *p) {
  Doc *d = D(e);
  const Theme *t = th(e);
  const HlSyntax *syn = hl_select(d->path);
  const size_t *vp;
  Popup box;
  int w, h, digits, status_y, in_ml;

  plat_get_size(p, &w, &h);
  ensure_lines(d);
  sync_caret_rc(d);
  layout(e, p, d, w, h, &digits);
  vp = vrow_table(e, d);
  if (e->mode == MODE_EDIT) find_bracket(d);
  in_ml = hl_state_at_view(d, syn);
  status_y = h > e->ch ? h - e->ch : 0;

  plat_begin_frame(p);
  plat_clear(p, t->bg);
  if (e->gutter > 0) plat_fill_rect(p, 0, 0, e->gutter, status_y, t->gutter_bg);
  draw_text_rows(e, d, p, t, syn, in_ml, digits, w);
  draw_caret(e, d, p, t, vp);
  draw_status(e, d, p, t, syn, w, h, status_y);
  if (popup_open(e)) {
    popup_layout(e, w, status_y, &box);
    if (e->mode == MODE_HELP) draw_help(e, p, t, &box);
    else draw_list_popup(e, p, t, &box);
  }
  update_title(e, p, d);
  plat_end_frame(p);
  e->need_draw = MOTE_FALSE;
}
