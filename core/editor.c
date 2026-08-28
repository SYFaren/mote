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
static void view_invalid(Doc *d) { d->row0_valid = MOTE_FALSE; }

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

static void lines_mark_dirty(Doc *d) {
  d->lines.dirty = MOTE_TRUE;
  d->row0_valid = MOTE_FALSE;
  d->hl_ml_valid = MOTE_FALSE;
}

static void lines_shift(Doc *d, size_t pos, long delta) {
  size_t i;
  if (d->lines.dirty || !d->lines.off || !delta) return;
  for (i = 0; i < d->lines.n; i++) {
    if (d->lines.off[i] > pos)
      d->lines.off[i] = (size_t)((long)d->lines.off[i] + delta);
  }
  d->row0_valid = MOTE_FALSE;
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
static void unsaved_ask(Editor *e, const char *verb) {
  char b[72];
  e->prompt[0] = 0;
  mote_snprintf(b, sizeof b, "Unsaved — ^S %s  ^Q discard  Esc", verb);
  set_status(e, b);
}

static mote_bool prompt_accepts_input(EdMode mode) {
  return mode == MODE_OPEN || mode == MODE_SAVEAS || mode == MODE_FIND ||
         mode == MODE_REPLACE || mode == MODE_GOTO;
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
    if (d->bm_row[i] != (size_t)-1 && d->bm_row[i] >= d->lines.n)
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
    if (br == (size_t)-1) continue;
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
    if (br == (size_t)-1) continue;
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

static void caret_vis(Editor *e, Doc *d, size_t *vr, size_t *vc) {
  size_t r;
  *vr = 0;
  for (r = 0; r < d->caret_row; r++) *vr += segs_of(e, d, r);
  if (e->wrap && e->cols > 0) {
    if (d->caret_col > 0 && (d->caret_col % (size_t)e->cols) == 0) {
      *vr += d->caret_col / (size_t)e->cols - 1;
      *vc = (size_t)e->cols; /* past last cell of segment */
    } else {
      *vr += d->caret_col / (size_t)e->cols;
      *vc = d->caret_col % (size_t)e->cols;
    }
  } else {
    *vc = d->caret_col;
  }
}

static size_t view_vrow0(Editor *e, Doc *d) {
  size_t r, v = 0;
  for (r = 0; r < d->row0; r++) v += segs_of(e, d, r);
  return v + d->wrap0;
}

static size_t view_vrow0_vp(Editor *e, Doc *d, const size_t *vp) {
  if (vp) return vp[d->row0] + d->wrap0;
  return view_vrow0(e, d);
}

static void caret_vis_vp(Editor *e, Doc *d, size_t *vr, size_t *vc,
                         const size_t *vp) {
  if (vp) {
    *vr = vp[d->caret_row];
    if (e->wrap && e->cols > 0) {
      if (d->caret_col > 0 && (d->caret_col % (size_t)e->cols) == 0) {
        *vr += d->caret_col / (size_t)e->cols - 1;
        *vc = (size_t)e->cols;
      } else {
        *vr += d->caret_col / (size_t)e->cols;
        *vc = d->caret_col % (size_t)e->cols;
      }
    } else {
      *vc = d->caret_col;
    }
    return;
  }
  caret_vis(e, d, vr, vc);
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

static void set_view_vrow(Editor *e, Doc *d, size_t want) {
  size_t r = 0, acc = 0, n, total = 0;
  ensure_lines(d);
  n = d->lines.n ? d->lines.n : 1;
  for (r = 0; r < n; r++) total += segs_of(e, d, r);
  if (total == 0) total = 1;
  if (want >= total) want = total - 1;
  r = 0;
  acc = 0;
  while (r < n) {
    size_t s = segs_of(e, d, r);
    if (acc + s > want) {
      d->row0 = r;
      d->wrap0 = want - acc;
      view_invalid(d);
      return;
    }
    acc += s;
    r++;
  }
  d->row0 = n ? n - 1 : 0;
  d->wrap0 = 0;
  view_invalid(d);
}

static void sync_caret_rc(Doc *d) {
  pos_to_rc(d, d->caret, &d->caret_row, &d->caret_col);
  d->pref_col = d->caret_col;
}

static void ensure_visible(Editor *e, Doc *d) {
  size_t vr, vc, top, old_r = d->row0, old_w = d->wrap0;
  if (e->wrap) d->col0 = 0;
  caret_vis(e, d, &vr, &vc);
  top = view_vrow0(e, d);
  if (vr < top)
    set_view_vrow(e, d, vr);
  else if (e->rows > 0 && vr >= top + (size_t)e->rows)
    set_view_vrow(e, d, vr - (size_t)e->rows + 1);
  if (!e->wrap) {
    if (d->caret_col < d->col0) d->col0 = d->caret_col;
    if (e->cols > 0 && d->caret_col >= d->col0 + (size_t)e->cols)
      d->col0 = d->caret_col - (size_t)e->cols + 1;
  }
  if (d->row0 != old_r || d->wrap0 != old_w) view_invalid(d);
}

static void clamp_caret(Doc *d) {
  size_t len = buf_len(&d->buf);
  if (d->caret > len) d->caret = len;
  if (d->sel_anchor > len) d->sel_anchor = len;
}

static mote_bool can_edit(const Doc *d) { return !d->readonly; }

static mote_bool push_delete(Editor *e, Doc *d, size_t pos, size_t n) {
  char *t;
  size_t i;
  mote_bool has_nl = MOTE_FALSE;
  if (!n || !can_edit(d)) return MOTE_FALSE;
  t = (char *)malloc(n);
  if (!t) {
    set_status(e, "out of memory");
    return MOTE_FALSE;
  }
  buf_get(&d->buf, pos, n, t);
  for (i = 0; i < n; i++) {
    if (t[i] == '\n') {
      has_nl = MOTE_TRUE;
      break;
    }
  }
  if (!undo_push(&d->undo, U_DELETE, pos, t, n, MOTE_FALSE)) {
    free(t);
    set_status(e, "out of memory");
    return MOTE_FALSE;
  }
  if (has_nl) bm_line_delete(d, pos, count_nl(t, n));
  free(t);
  buf_delete(&d->buf, pos, n);
  d->dirty = MOTE_TRUE;
  clamp_caret(d);
  if (has_nl) lines_mark_dirty(d);
  else lines_shift(d, pos, -(long)n);
  mark(e);
  return MOTE_TRUE;
}

static mote_bool push_insert(Editor *e, Doc *d, size_t pos, const char *s, size_t n,
                        mote_bool coalesce) {
  size_t i;
  mote_bool has_nl = MOTE_FALSE;
  if (!n || !s || !can_edit(d)) return MOTE_FALSE;
  for (i = 0; i < n; i++) {
    if (s[i] == '\n') {
      has_nl = MOTE_TRUE;
      break;
    }
  }
  if (has_nl) bm_line_insert(d, pos, count_nl(s, n));
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
  if (has_nl) {
    lines_mark_dirty(d);
    bm_clamp_rows(d);
  }
  else lines_shift(d, pos, (long)n);
  mark(e);
  return MOTE_TRUE;
}

static void delete_sel(Editor *e, Doc *d) {
  size_t a, b;
  if (!has_sel(d) || !can_edit(d)) return;
  a = sel_lo(d);
  b = sel_hi(d);
  push_delete(e, d, a, b - a);
  d->caret = a;
  clear_sel(d);
  sync_caret_rc(d);
}

static void insert_text(Editor *e, Doc *d, const char *s, size_t n) {
  mote_bool coal;
  if (!can_edit(d)) {
    set_status(e, "readonly");
    return;
  }
  coal = (n <= 4 && n > 0 && s[0] != '\n' && s[0] != '\t');
  delete_sel(e, d);
  push_insert(e, d, d->caret, s, n, coal);
  d->caret += n;
  clear_sel(d);
  sync_caret_rc(d);
  ensure_visible(e, d);
}

static void insert_autoclose(Editor *e, Doc *d, char open, char close) {
  char pair[2];
  if (!can_edit(d)) {
    set_status(e, "readonly");
    return;
  }
  if (has_sel(d)) {
    size_t lo = sel_lo(d), hi = sel_hi(d);
    pair[0] = open;
    if (!push_insert(e, d, lo, pair, 1, MOTE_FALSE)) return;
    hi += 1;
    pair[0] = close;
    if (!push_insert(e, d, hi, pair, 1, MOTE_FALSE)) return;
    d->caret = lo + 1;
    clear_sel(d);
    sync_caret_rc(d);
    ensure_visible(e, d);
    return;
  }
  pair[0] = open;
  pair[1] = close;
  if (!push_insert(e, d, d->caret, pair, 2, MOTE_FALSE)) return;
  d->caret += 1;
  clear_sel(d);
  sync_caret_rc(d);
  ensure_visible(e, d);
}

static void apply_undo_act(Editor *e, Doc *d, UndoAct *a, int redo) {
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
  clear_sel(d);
  clamp_caret(d);
  sync_caret_rc(d);
  d->dirty = MOTE_TRUE;
  ensure_visible(e, d);
  lines_mark_dirty(d);
  mark(e);
}

static void do_undo(Editor *e, Doc *d) {
  UndoAct *a;
  if (!can_edit(d)) {
    set_status(e, "readonly");
    return;
  }
  a = undo_pop_undo(&d->undo);
  if (a) apply_undo_act(e, d, a, 0);
}

static void do_redo(Editor *e, Doc *d) {
  UndoAct *a;
  if (!can_edit(d)) {
    set_status(e, "readonly");
    return;
  }
  a = undo_pop_redo(&d->undo);
  if (a) apply_undo_act(e, d, a, 1);
}

static void move_caret(Editor *e, Doc *d, size_t np, mote_bool keep_sel) {
  d->caret = np;
  clamp_caret(d);
  buf_seek(&d->buf, d->caret);
  if (!keep_sel) clear_sel(d);
  sync_caret_rc(d);
  ensure_visible(e, d);
  mark(e);
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
  caret_vis(e, d, &vr, &vc);
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

static void normalize_eol(Doc *d) {
  size_t i, len = buf_len(&d->buf), w = 0;
  char *tmp;
  Buf nb;
  int crlf = 0, saw_cr = 0;
  for (i = 0; i < len; i++) {
    if (buf_at(&d->buf, i) == '\r') {
      saw_cr = 1;
      if (i + 1 < len && buf_at(&d->buf, i + 1) == '\n') crlf = 1;
    }
  }
  d->eol = crlf ? EOL_CRLF : EOL_LF;
  if (!saw_cr) return; /* already LF-only — skip rebuild */
  tmp = (char *)malloc(len + 1);
  if (!tmp) return;
  for (i = 0; i < len; i++) {
    char c = buf_at(&d->buf, i);
    if (c == '\r') {
      if (i + 1 < len && buf_at(&d->buf, i + 1) == '\n') continue;
      tmp[w++] = '\n';
    } else
      tmp[w++] = c;
  }
  if (!buf_init(&nb, w)) {
    free(tmp);
    return;
  }
  if (w && !buf_insert(&nb, 0, tmp, w)) {
    buf_free(&nb);
    free(tmp);
    return;
  }
  free(tmp);
  buf_free(&d->buf);
  d->buf = nb;
}

static mote_bool save_to(Editor *e, Doc *d, const char *path) {
  if (!path || !path[0]) {
    set_status(e, "empty path");
    return MOTE_FALSE;
  }
  if (d->eol == EOL_CRLF) {
    Buf tb;
    size_t i, len = buf_len(&d->buf);
    if (!buf_init(&tb, len + len / 8 + 8)) {
      set_status(e, "out of memory");
      return MOTE_FALSE;
    }
    for (i = 0; i < len; i++) {
      char c = buf_at(&d->buf, i);
      if (c == '\n') {
        if (!buf_insert(&tb, buf_len(&tb), "\r\n", 2)) {
          buf_free(&tb);
          set_status(e, "out of memory");
          return MOTE_FALSE;
        }
      } else if (!buf_insert(&tb, buf_len(&tb), &c, 1)) {
        buf_free(&tb);
        set_status(e, "out of memory");
        return MOTE_FALSE;
      }
    }
    if (!buf_save(&tb, path)) {
      buf_free(&tb);
      set_status(e, "save failed");
      return MOTE_FALSE;
    }
    buf_free(&tb);
  } else if (!buf_save(&d->buf, path)) {
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
  else {
    e->mode = MODE_SAVEAS;
    e->prompt[0] = 0;
    set_status(e, "Save As:");
  }
}

static mote_bool match_at(Editor *e, Doc *d, size_t i, size_t *out_len) {
  size_t len = buf_len(&d->buf);
  size_t flen;
  if (!e->find[0]) return MOTE_FALSE;
  if (e->find_regex) {
    flen = re_match_buf(&d->buf, i, e->find, e->find_case);
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

static void find_next(Editor *e, Doc *d) {
  size_t len, i, mlen;
  if (!e->find[0]) {
    set_status(e, "no pattern");
    return;
  }
  len = buf_len(&d->buf);
  if (!len) {
    set_status(e, "not found");
    return;
  }
  i = d->caret < len ? ed_next(d, d->caret) : len;
  for (; i < len; i = ed_next(d, i)) {
    if (match_at(e, d, i, &mlen)) {
      apply_match(e, d, i, mlen, "found");
      return;
    }
  }
  for (i = 0; i < len && i <= d->caret; i = ed_next(d, i)) {
    if (match_at(e, d, i, &mlen)) {
      apply_match(e, d, i, mlen, "found (wrap)");
      return;
    }
  }
  d->match_a = d->match_b = 0;
  set_status(e, "not found");
}

static void find_prev(Editor *e, Doc *d) {
  size_t len, i, lim, best = (size_t)-1, best_m = 0, mlen;
  if (!e->find[0]) {
    set_status(e, "no pattern");
    return;
  }
  len = buf_len(&d->buf);
  if (!len) {
    set_status(e, "not found");
    return;
  }
  lim = has_sel(d) ? sel_lo(d) : d->caret;
  for (i = 0; i < lim; i = ed_next(d, i)) {
    if (match_at(e, d, i, &mlen)) {
      best = i;
      best_m = mlen;
    }
  }
  if (best != (size_t)-1) {
    apply_match(e, d, best, best_m, "found");
    return;
  }
  best = (size_t)-1;
  for (i = 0; i < len; i = ed_next(d, i)) {
    if (match_at(e, d, i, &mlen)) {
      best = i;
      best_m = mlen;
    }
  }
  if (best != (size_t)-1) {
    apply_match(e, d, best, best_m, "found (wrap)");
    return;
  }
  d->match_a = d->match_b = 0;
  set_status(e, "not found");
}

static void find_bracket(Doc *d) {
  static const char openers[] = "([{";
  static const char closers[] = ")]}";
  size_t len = buf_len(&d->buf), pos = d->caret;
  char ch;
  int dir, depth, i;
  const char *pair;
  d->bracket_a = d->bracket_b = (size_t)-1;
  if (len == 0) return;
  if (pos >= len) pos = len - 1;
  ch = buf_at(&d->buf, pos);
  pair = strchr(openers, ch);
  if (pair) {
    dir = 1;
    i = (int)(pair - openers);
  } else {
    pair = strchr(closers, ch);
    if (!pair) {
      if (pos == 0) return;
      pos--;
      ch = buf_at(&d->buf, pos);
      pair = strchr(openers, ch);
      if (pair) {
        dir = 1;
        i = (int)(pair - openers);
      } else {
        pair = strchr(closers, ch);
        if (!pair) return;
        dir = -1;
        i = (int)(pair - closers);
      }
    } else {
      dir = -1;
      i = (int)(pair - closers);
    }
  }
  d->bracket_a = pos;
  depth = 1;
  for (;;) {
    if (dir > 0) {
      if (pos + 1 >= len) {
        d->bracket_a = (size_t)-1;
        return;
      }
      pos++;
    } else {
      if (pos == 0) {
        d->bracket_a = (size_t)-1;
        return;
      }
      pos--;
    }
    ch = buf_at(&d->buf, pos);
    if (ch == openers[i]) {
      if (dir > 0)
        depth++;
      else if (--depth == 0) {
        d->bracket_b = pos;
        return;
      }
    } else if (ch == closers[i]) {
      if (dir < 0)
        depth++;
      else if (--depth == 0) {
        d->bracket_b = pos;
        return;
      }
    }
  }
}

static void do_replace_all(Editor *e, Doc *d) {
  size_t rlen, i, count = 0, mlen;
  if (!can_edit(d)) {
    set_status(e, "readonly");
    return;
  }
  if (!e->find[0]) {
    set_status(e, "find first (Ctrl+F)");
    return;
  }
  rlen = strlen(e->replace);
  i = 0;
  while (i < buf_len(&d->buf)) {
    if (!match_at(e, d, i, &mlen)) {
      i = ed_next(d, i);
      continue;
    }
    if (!push_delete(e, d, i, mlen)) {
      set_status(e, "replace aborted");
      break;
    }
    if (rlen && !push_insert(e, d, i, e->replace, rlen, MOTE_FALSE)) {
      set_status(e, "replace aborted");
      break;
    }
    count++;
    i += rlen;
  }
  clear_sel(d);
  sync_caret_rc(d);
  ensure_visible(e, d);
  {
    char msg[64];
    mote_snprintf(msg, sizeof msg, "replaced %lu", (unsigned long)count);
    set_status(e, msg);
  }
  mark(e);
}

static int parse_slash_cmd(const char *in, char *pat, size_t patn, char *repl,
                           size_t repln) {
  const char *p, *q, *r;
  size_t n;
  if (!in || in[0] != '/' || !pat || patn < 2) return 0;
  p = in + 1;
  q = p;
  while (*q && *q != '/') {
    if (*q == '\\' && q[1]) q += 2;
    else q++;
  }
  if (*q != '/') return 0;
  n = (size_t)(q - p);
  if (n >= patn) n = patn - 1;
  memcpy(pat, p, n);
  pat[n] = 0;
  if (repl && repln) {
    repl[0] = 0;
    r = q + 1;
    p = r;
    while (*r && *r != '/') {
      if (*r == '\\' && r[1]) r += 2;
      else r++;
    }
    if (*r == '/') {
      n = (size_t)(r - p);
      if (n >= repln) n = repln - 1;
      memcpy(repl, p, n);
      repl[n] = 0;
    }
  }
  return 1;
}

static const char *comment_prefix(Doc *d) {
  const char *lang = hl_lang_name(hl_select(d->path));
  if (strcmp(lang, "python") == 0 || strcmp(lang, "shell") == 0 ||
      strcmp(lang, "yaml") == 0)
    return "#";
  return "//";
}

static size_t line_indent_pos(Doc *d, size_t row) {
  size_t pos = row_start(d, row);
  size_t end = line_end(d, pos);
  while (pos < end) {
    char c = buf_at(&d->buf, pos);
    if (c != ' ' && c != '\t') break;
    pos++;
  }
  return pos;
}

static void toggle_comment(Editor *e, Doc *d) {
  size_t r0, r1, row, pos, pfxlen, end;
  const char *pfx;
  int all = 1, any = 0;
  if (!can_edit(d)) {
    set_status(e, "readonly");
    return;
  }
  pfx = comment_prefix(d);
  pfxlen = strlen(pfx);
  ensure_lines(d);
  if (!has_sel(d)) {
    r0 = r1 = d->caret_row;
  } else {
    pos_to_rc(d, sel_lo(d), &r0, &row);
    pos_to_rc(d, sel_hi(d) > 0 ? sel_hi(d) - 1 : sel_hi(d), &r1, &row);
    (void)row;
  }
  for (row = r0; row <= r1; row++) {
    size_t k;
    pos = line_indent_pos(d, row);
    end = line_end(d, row_start(d, row));
    if (pos >= end) continue;
    any = 1;
    if ((size_t)(end - pos) < pfxlen) {
      all = 0;
      continue;
    }
    for (k = 0; k < pfxlen; k++) {
      if (buf_at(&d->buf, pos + k) != pfx[k]) {
        all = 0;
        break;
      }
    }
  }
  if (!any) return;
  for (row = r0; row <= r1; row++) {
    pos = line_indent_pos(d, row);
    end = line_end(d, row_start(d, row));
    if (pos >= end) continue;
    if (all) {
      size_t k;
      for (k = 0; k < pfxlen; k++) {
        if (pos + k >= end || buf_at(&d->buf, pos + k) != pfx[k]) break;
      }
      if (k == pfxlen) push_delete(e, d, pos, pfxlen);
    } else {
      push_insert(e, d, pos, pfx, pfxlen, MOTE_FALSE);
    }
  }
  sync_caret_rc(d);
  ensure_visible(e, d);
  set_status(e, all ? "uncommented" : "commented");
  mark(e);
}

static int fuzzy_score(const char *q, const char *name) {
  int score = 0, streak = 0;
  const char *p = q;
  const char *s = name;
  if (!q || !q[0]) return 0;
  if (!name) return -1;
  while (*p) {
    char qc = *p;
    char found = 0;
    if (qc >= 'A' && qc <= 'Z') qc = (char)(qc - 'A' + 'a');
    for (; *s; s++) {
      char sc = *s;
      if (sc >= 'A' && sc <= 'Z') sc = (char)(sc - 'A' + 'a');
      if (sc == qc) {
        found = 1;
        break;
      }
    }
    if (!found) return -1;
    if (s == name) score += 20;
    streak++;
    score += 5 + streak;
    p++;
    s++;
  }
  score -= (int)strlen(name) / 4;
  return score;
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

static void qf_sort_names(char names[][256], int n) {
  int i, j;
  for (i = 0; i + 1 < n; i++) {
    for (j = i + 1; j < n; j++) {
      if (strcmp(names[i], names[j]) > 0) {
        char tmp[256];
        memcpy(tmp, names[i], sizeof tmp);
        memcpy(names[i], names[j], sizeof names[j]);
        memcpy(names[j], tmp, sizeof tmp);
      }
    }
  }
}

static void quickopen_filter(Editor *e) {
  int i, n = 0;
  const char *q = e->prompt;
  for (i = 0; i < e->qf_pool_n && n < QF_MAX; i++) {
    int sc = fuzzy_score(q, e->qf_pool[i]);
    if (sc < 0) continue;
    mote_snprintf(e->qf_match[n], sizeof e->qf_match[0], "%s", e->qf_pool[i]);
    n++;
  }
  if (!q[0]) {
    n = e->qf_pool_n;
    if (n > QF_MAX) n = QF_MAX;
    for (i = 0; i < n; i++)
      mote_snprintf(e->qf_match[i], sizeof e->qf_match[0], "%s", e->qf_pool[i]);
  }
  e->qf_n = n;
  if (e->qf_sel >= n) e->qf_sel = n > 0 ? n - 1 : 0;
}

static void quickopen_begin(Editor *e, Doc *d) {
  path_dir_of(d->path[0] ? d->path : ".", e->qf_dir, sizeof e->qf_dir);
  e->qf_pool_n = dirlist_files(e->qf_dir, e->qf_pool, QF_POOL);
  qf_sort_names(e->qf_pool, e->qf_pool_n);
  e->prompt[0] = 0;
  e->qf_sel = 0;
  quickopen_filter(e);
  e->mode = MODE_QUICKOPEN;
  set_status(e, "Go to file — type filter  j/k Enter");
  mark(e);
}

static void goto_line(Editor *e, Doc *d, size_t line1) {
  size_t row;
  ensure_lines(d);
  if (line1 == 0) line1 = 1;
  row = line1 - 1;
  if (d->lines.n && row >= d->lines.n) row = d->lines.n - 1;
  d->caret = row_start(d, row);
  clear_sel(d);
  sync_caret_rc(d);
  ensure_visible(e, d);
  set_status(e, "ok");
  mark(e);
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

static void bookmark_toggle(Editor *e, Doc *d) {
  size_t row;
  int i, slot = 0;
  row = bookmark_target_row(d);
  for (i = 0; i < MAX_BOOKMARKS; i++) {
    if (d->bm_row[i] == row) {
      d->bm_row[i] = (size_t)-1;
      set_status(e, "mark cleared");
      mark(e);
      return;
    }
  }
  for (i = 0; i < MAX_BOOKMARKS; i++) {
    if (d->bm_row[i] == (size_t)-1) {
      slot = i;
      break;
    }
    slot = i + 1;
  }
  if (slot >= MAX_BOOKMARKS) slot = 0;
  d->bm_row[slot] = row;
  {
    char msg[32];
    mote_snprintf(msg, sizeof msg, "mark %d", slot + 1);
    set_status(e, msg);
  }
  mark(e);
}

static void bookmark_jump(Editor *e, Doc *d) {
  int tries = 0;
  sync_caret_rc(d);
  if (d->bm_row[d->bm_jump] == (size_t)-1) d->bm_jump = 0;
  for (tries = 0; tries < MAX_BOOKMARKS; tries++) {
    if (d->bm_row[d->bm_jump] != (size_t)-1) {
      d->caret = row_start(d, d->bm_row[d->bm_jump]);
      clear_sel(d);
      sync_caret_rc(d);
      ensure_visible(e, d);
      {
        char msg[32];
        mote_snprintf(msg, sizeof msg, "mark %d", d->bm_jump + 1);
        set_status(e, msg);
      }
      d->bm_jump = (d->bm_jump + 1) % MAX_BOOKMARKS;
      mark(e);
      return;
    }
    d->bm_jump = (d->bm_jump + 1) % MAX_BOOKMARKS;
  }
  set_status(e, "no bookmarks");
}

static void insert_newline_indent(Editor *e, Doc *d) {
  size_t ls = line_start(d, d->caret);
  size_t i = ls, n = 0;
  char ind[160];
  ind[0] = '\n';
  n = 1;
  while (i < d->caret && n + 1 < sizeof ind) {
    char c = buf_at(&d->buf, i);
    if (c != ' ' && c != '\t') break;
    ind[n++] = c;
    i++;
  }
  insert_text(e, d, ind, n);
}

static void indent_sel(Editor *e, Doc *d, int dir) {
  size_t a, b, row, r0, r1, pos;
  if (!can_edit(d)) {
    set_status(e, "readonly");
    return;
  }
  if (!has_sel(d)) {
    if (dir > 0) {
      insert_text(e, d, "\t", 1);
      return;
    }
    r0 = r1 = d->caret_row;
    a = b = d->caret;
  } else {
    a = sel_lo(d);
    b = sel_hi(d);
    pos_to_rc(d, a, &r0, &row);
    pos_to_rc(d, b > 0 ? b - 1 : b, &r1, &row);
    (void)row;
  }
  for (row = r0; row <= r1; row++) {
    pos = row_start(d, row);
    if (dir > 0) {
      if (!push_insert(e, d, pos, "\t", 1, MOTE_FALSE)) break;
      if (a >= pos) a++;
      b++;
    } else {
      char c0;
      if (pos >= buf_len(&d->buf)) continue;
      c0 = buf_at(&d->buf, pos);
      if (c0 == '\t') {
        if (!push_delete(e, d, pos, 1)) break;
        if (a > pos) a--;
        if (b > pos) b--;
      } else if (c0 == ' ') {
        size_t n = 0;
        while (n < 4 && pos + n < buf_len(&d->buf) &&
               buf_at(&d->buf, pos + n) == ' ')
          n++;
        if (n) {
          if (!push_delete(e, d, pos, n)) break;
          if (a > pos) a -= (a - pos < n ? a - pos : n);
          if (b > pos) b -= (b - pos < n ? b - pos : n);
        }
      }
    }
  }
  d->sel_anchor = a;
  d->caret = b;
  sync_caret_rc(d);
  ensure_visible(e, d);
  mark(e);
}

static void delete_line(Editor *e, Doc *d) {
  size_t a, b, len;
  if (!can_edit(d)) {
    set_status(e, "readonly");
    return;
  }
  a = line_start(d, d->caret);
  b = line_end(d, d->caret);
  len = buf_len(&d->buf);
  if (b < len && buf_at(&d->buf, b) == '\n') b++;
  else if (a > 0 && b == len) {
    a--; /* eat preceding newline if last line */
  }
  push_delete(e, d, a, b - a);
  d->caret = a > len ? len : a;
  if (d->caret > buf_len(&d->buf)) d->caret = buf_len(&d->buf);
  clear_sel(d);
  sync_caret_rc(d);
  ensure_visible(e, d);
}

static void dup_line(Editor *e, Doc *d) {
  size_t a, b, n;
  char *s;
  if (!can_edit(d)) {
    set_status(e, "readonly");
    return;
  }
  a = line_start(d, d->caret);
  b = line_end(d, d->caret);
  if (b < buf_len(&d->buf) && buf_at(&d->buf, b) == '\n') {
    b++;
    n = b - a;
    s = slice_dup(d, a, b);
    if (!s) return;
    if (!push_insert(e, d, b, s, n, MOTE_FALSE)) {
      free(s);
      return;
    }
    free(s);
    d->caret = b + (d->caret - a);
  } else {
    n = b - a;
    s = slice_dup(d, a, b);
    if (!s) return;
    if (!push_insert(e, d, b, "\n", 1, MOTE_FALSE)) {
      free(s);
      return;
    }
    if (n && !push_insert(e, d, b + 1, s, n, MOTE_FALSE)) {
      free(s);
      return;
    }
    free(s);
    d->caret = b + 1 + (d->caret - a);
  }
  clear_sel(d);
  sync_caret_rc(d);
  ensure_visible(e, d);
}

static void cycle_theme(Editor *e) {
  int n = theme_count();
  e->theme_id = (e->theme_id + 1) % n;
  {
    char msg[64];
    mote_snprintf(msg, sizeof msg, "theme: %s", theme_name(e->theme_id));
    set_status(e, msg);
  }
  mark(e);
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
  if (!can_edit(d)) {
    set_status(e, "readonly");
    return;
  }
  copy_sel(e, d, p);
  delete_sel(e, d);
  ensure_visible(e, d);
}

static void paste_clip(Editor *e, Doc *d, Plat *p) {
  size_t n = 0, room;
  char *s;
  if (!can_edit(d)) {
    set_status(e, "readonly");
    return;
  }
  s = plat_clipboard_get(p, &n);
  if (!s) return;
  room = MOTE_MAX_FILE - buf_len(&d->buf);
  if (n > room) {
    if (!room) {
      set_status(e, "file at size limit");
      free(s);
      return;
    }
    n = room;
    set_status(e, "paste truncated");
  }
  delete_sel(e, d);
  push_insert(e, d, d->caret, s, n, MOTE_FALSE);
  d->caret += n;
  clear_sel(d);
  sync_caret_rc(d);
  ensure_visible(e, d);
  free(s);
}

static size_t click_to_pos(Editor *e, Doc *d, int mx, int my) {
  size_t row, col, vr, vc;
  if (e->ch <= 0 || e->cw <= 0) return d->caret;
  if (my < 0) my = 0;
  if (e->rows > 0 && my >= e->rows * e->ch) my = e->rows * e->ch - 1;
  mx -= e->gutter;
  if (mx < 0) mx = 0;
  vr = view_vrow0(e, d) + (size_t)(my / e->ch);
  vc = e->wrap ? (size_t)(mx / e->cw) : d->col0 + (size_t)(mx / e->cw);
  vis_to_pos(e, d, vr, vc, &row, &col);
  return rc_to_pos(d, row, col);
}

static void doc_reset(Doc *d) {
  int i;
  memset(d, 0, sizeof *d);
  d->lines.dirty = MOTE_TRUE;
  d->bracket_a = d->bracket_b = (size_t)-1;
  for (i = 0; i < MAX_BOOKMARKS; i++) d->bm_row[i] = (size_t)-1;
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

static int any_dirty(const Editor *e);

mote_bool ed_init(Editor *e) {
  memset(e, 0, sizeof *e);
  e->ndocs = 1;
  e->cur = 0;
  if (!doc_init_empty(D(e))) return MOTE_FALSE;
  e->theme_id = 0;
  e->status[0] = 0;
  e->need_draw = MOTE_TRUE;
  if (getenv("MOTE_START_HELP")) e->mode = MODE_HELP;
  return MOTE_TRUE;
}

void ed_free(Editor *e) {
  int i;
  free(e->vrow_cache);
  e->vrow_cache = NULL;
  for (i = 0; i < e->ndocs; i++) doc_free(&e->docs[i]);
}

mote_bool ed_open_path(Editor *e, const char *path) {
  Doc *d = D(e);
  Buf nb;

  if (d->dirty) {
    if (e->ndocs < MAX_DOCS) {
      ed_new_doc(e);
      d = D(e);
    } else {
      mote_snprintf(e->pending_path, sizeof e->pending_path, "%s", path);
      e->mode = MODE_OPENASK;
      unsaved_ask(e, "open");
      return MOTE_FALSE;
    }
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
    buf_free(&d->buf);
    if (!buf_init(&d->buf, 0)) return MOTE_FALSE;
    mote_snprintf(d->path, sizeof d->path, "%s", path);
    d->dirty = MOTE_FALSE;
    d->readonly = MOTE_FALSE;
    d->eol = EOL_LF;
    d->caret = d->sel_anchor = 0;
    d->row0 = d->col0 = d->wrap0 = 0;
    d->caret_row = d->caret_col = 0;
    lines_mark_dirty(d);
    undo_free(&d->undo);
    undo_init(&d->undo);
    recent_add(e, path);
    set_status(e, "new file");
    return MOTE_TRUE;
  }
  buf_free(&d->buf);
  d->buf = nb;
  normalize_eol(d);
  mote_snprintf(d->path, sizeof d->path, "%s", path);
  d->dirty = MOTE_FALSE;
  d->readonly = MOTE_FALSE;
  d->caret = d->sel_anchor = 0;
  d->row0 = d->col0 = d->wrap0 = 0;
  d->caret_row = d->caret_col = 0;
  lines_mark_dirty(d);
  undo_free(&d->undo);
  undo_init(&d->undo);
  recent_add(e, path);
  set_status(e, "opened");
  return MOTE_TRUE;
}

static void switch_doc(Editor *e, int idx) {
  if (idx < 0 || idx >= e->ndocs) return;
  e->cur = idx;
  {
    char msg[48];
    mote_snprintf(msg, sizeof msg, "doc %d/%d", e->cur + 1, e->ndocs);
    set_status(e, msg);
  }
  mark(e);
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
  set_status(e, "new doc");
  mark(e);
}

static void close_doc_force(Editor *e) {
  Doc *d = D(e);
  if (e->ndocs <= 1) {
    doc_free(d);
    if (!doc_init_empty(d)) {
      set_status(e, "out of memory");
      return;
    }
    set_status(e, "closed");
    mark(e);
    return;
  }
  doc_free(d);
  if (e->cur < e->ndocs - 1)
    memmove(&e->docs[e->cur], &e->docs[e->cur + 1],
            (size_t)(e->ndocs - e->cur - 1) * sizeof e->docs[0]);
  e->ndocs--;
  if (e->cur >= e->ndocs) e->cur = e->ndocs - 1;
  {
    char msg[48];
    mote_snprintf(msg, sizeof msg, "doc %d/%d", e->cur + 1, e->ndocs);
    set_status(e, msg);
  }
  mark(e);
}

static void close_doc(Editor *e) {
  Doc *d = D(e);
  if (d->dirty) {
    e->mode = MODE_CLOSEASK;
    unsaved_ask(e, "close");
    return;
  }
  close_doc_force(e);
}

static void finish_quit_saves(Editor *e) {
  int i;
  for (i = 0; i < e->ndocs; i++) {
    if (!e->docs[i].dirty) continue;
    if (!e->docs[i].path[0]) {
      e->cur = i;
      e->quit_after_save = MOTE_TRUE;
      e->mode = MODE_SAVEAS;
      e->prompt[0] = 0;
      set_status(e, "Save As:");
      return;
    }
    if (!save_to(e, &e->docs[i], e->docs[i].path)) return;
  }
  e->quit_after_save = MOTE_FALSE;
  if (!any_dirty(e)) e->want_quit = MOTE_TRUE;
}

static void jump_bracket(Editor *e, Doc *d) {
  find_bracket(d);
  if (d->bracket_a == (size_t)-1 || d->bracket_b == (size_t)-1) {
    set_status(e, "no match");
    return;
  }
  if (d->caret == d->bracket_b)
    move_caret(e, d, d->bracket_a, MOTE_FALSE);
  else
    move_caret(e, d, d->bracket_b, MOTE_FALSE);
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
  ed_open_path(e, d->path);
  set_status(e, "reloaded");
}

static void find_sync_prompt(Editor *e) {
  char pat[192], repl[192];
  if (e->mode != MODE_FIND && e->mode != MODE_REPLACE) return;
  if (parse_slash_cmd(e->prompt, pat, sizeof pat, repl, sizeof repl)) {
    mote_snprintf(e->find, sizeof e->find, "%s", pat);
    e->find_regex = MOTE_TRUE;
  } else {
    mote_snprintf(e->find, sizeof e->find, "%.255s", e->prompt);
    e->find_regex = MOTE_FALSE;
  }
}

static mote_bool handle_global_keys(Editor *e, Doc *d, const PlatEvent *ev) {
  if (ev->type != PE_KEY) return MOTE_FALSE;
  switch (ev->key) {
  case PK_BOOKMARK_SET:
    bookmark_toggle(e, d);
    return MOTE_TRUE;
  case PK_BOOKMARK:
    bookmark_jump(e, d);
    return MOTE_TRUE;
  case PK_FINDCASE:
    e->find_case = !e->find_case;
    set_status(e, e->find_case ? "find: case on" : "find: case off");
    return MOTE_TRUE;
  case PK_FINDWORD:
    e->find_word = !e->find_word;
    set_status(e, e->find_word ? "find: word on" : "find: word off");
    return MOTE_TRUE;
  case PK_FINDNEXT:
    find_sync_prompt(e);
    if (e->find[0]) find_next(e, d);
    else set_status(e, "type pattern, Enter");
    return MOTE_TRUE;
  case PK_FINDPREV:
    find_sync_prompt(e);
    if (e->find[0]) find_prev(e, d);
    else set_status(e, "type pattern, Enter");
    return MOTE_TRUE;
  default:
    return MOTE_FALSE;
  }
}

static void prompt_enter(Editor *e) {
  Doc *d = D(e);
  if (e->mode == MODE_OPEN) {
    if (e->prompt[0]) ed_open_path(e, e->prompt);
    e->mode = MODE_EDIT;
  } else if (e->mode == MODE_SAVEAS) {
    if (e->prompt[0] && save_to(e, d, e->prompt)) {
      if (e->quit_after_save) {
        e->quit_after_save = MOTE_FALSE;
        finish_quit_saves(e);
        mark(e);
        return;
      }
      if (e->close_after_save) {
        e->close_after_save = MOTE_FALSE;
        e->mode = MODE_EDIT;
        close_doc_force(e);
        mark(e);
        return;
      }
      if (e->pending_path[0]) {
        char path[1024];
        mote_snprintf(path, sizeof path, "%s", e->pending_path);
        e->pending_path[0] = 0;
        e->mode = MODE_EDIT;
        ed_open_path(e, path);
        mark(e);
        return;
      }
    }
    e->mode = MODE_EDIT;
  } else if (e->mode == MODE_FIND) {
    find_sync_prompt(e);
    e->mode = MODE_EDIT;
    find_next(e, d);
  } else if (e->mode == MODE_REPLACE) {
    char pat[192], repl[192];
    if (parse_slash_cmd(e->prompt, pat, sizeof pat, repl, sizeof repl)) {
      mote_snprintf(e->find, sizeof e->find, "%s", pat);
      mote_snprintf(e->replace, sizeof e->replace, "%s", repl);
      e->find_regex = MOTE_TRUE;
    } else {
      mote_snprintf(e->replace, sizeof e->replace, "%.255s", e->prompt);
    }
    e->mode = MODE_EDIT;
    do_replace_all(e, d);
  } else if (e->mode == MODE_GOTO) {
    size_t line = (size_t)strtoul(e->prompt, NULL, 10);
    e->mode = MODE_EDIT;
    goto_line(e, d, line);
  }
  mark(e);
}

static void handle_prompt(Editor *e, const PlatEvent *ev) {
  Doc *d = D(e);
  size_t n;
  if (handle_global_keys(e, d, ev)) return;
  if (ev->type == PE_KEY) {
    if (ev->key == PK_ESCAPE) {
      e->mode = MODE_EDIT;
      set_status(e, "F1 help");
      return;
    }
    if (ev->key == PK_ENTER) {
      prompt_enter(e);
      return;
    }
    if (ev->key == PK_BACKSPACE) {
      if (!prompt_accepts_input(e->mode)) return;
      n = strlen(e->prompt);
      if (n) {
        n = utf8_prev(e->prompt, n);
        e->prompt[n] = 0;
        mark(e);
      }
      return;
    }
  }
  if (ev->type == PE_TEXT && ev->text_len > 0 && prompt_accepts_input(e->mode)) {
    n = strlen(e->prompt);
    if ((e->mode == MODE_FIND || e->mode == MODE_REPLACE) && n > 0 &&
        ev->text[0] == '/' && e->prompt[0] != '/') {
      e->prompt[0] = 0;
      n = 0;
    }
    if (n + (size_t)ev->text_len < sizeof e->prompt - 1) {
      memcpy(e->prompt + n, ev->text, (size_t)ev->text_len);
      e->prompt[n + (size_t)ev->text_len] = 0;
      mark(e);
    }
  }
}

static int any_dirty(const Editor *e) {
  int i;
  for (i = 0; i < e->ndocs; i++)
    if (e->docs[i].dirty) return 1;
  return 0;
}

static void request_quit(Editor *e) {
  if (any_dirty(e)) {
    e->mode = MODE_QUITASK;
    unsaved_ask(e, "quit");
    return;
  }
  e->want_quit = MOTE_TRUE;
}

void ed_handle(Editor *e, Plat *p, const PlatEvent *ev) {
  mote_bool keep;
  size_t len, np;
  Doc *d = D(e);

  if (ev->type == PE_EXPOSE) {
    mark(e);
    return;
  }
  if (ev->type == PE_KEY && handle_global_keys(e, d, ev)) return;
  if (ev->type == PE_QUIT) {
    request_quit(e);
    return;
  }

  if (e->mode == MODE_HELP) {
    /* Only leave help on explicit dismiss — ignore focus noise / text. */
    if (ev->type == PE_KEY &&
        (ev->key == PK_ESCAPE || ev->key == PK_F1 || ev->key == PK_HELP ||
         ev->key == PK_ENTER)) {
      e->mode = MODE_EDIT;
      set_status(e, "F1 help");
      mark(e);
    }
    return;
  }

  if (e->mode == MODE_RECENT) {
    if (ev->type == PE_KEY) {
      if (ev->key == PK_ESCAPE) {
        e->mode = MODE_EDIT;
        set_status(e, "F1 help");
        return;
      }
      if (ev->key == PK_UP) {
        if (e->recent_sel > 0) e->recent_sel--;
        mark(e);
        return;
      }
      if (ev->key == PK_DOWN) {
        if (e->nrecent && e->recent_sel + 1 < e->nrecent) e->recent_sel++;
        mark(e);
        return;
      }
      if (ev->key == PK_ENTER) {
        if (e->nrecent > 0) ed_open_path(e, e->recent[e->recent_sel]);
        e->mode = MODE_EDIT;
        return;
      }
      /* digits 1-8 via text */
    }
    if (ev->type == PE_TEXT && ev->text_len == 1 && ev->text[0] >= '1' &&
        ev->text[0] <= '8') {
      int idx = ev->text[0] - '1';
      if (idx < e->nrecent) {
        ed_open_path(e, e->recent[idx]);
        e->mode = MODE_EDIT;
      }
      return;
    }
    if (ev->type == PE_TEXT && (ev->text[0] == 'j' || ev->text[0] == 'k')) {
      if (ev->text[0] == 'k' && e->recent_sel > 0) e->recent_sel--;
      if (ev->text[0] == 'j' && e->nrecent && e->recent_sel + 1 < e->nrecent)
        e->recent_sel++;
      mark(e);
      return;
    }
    return;
  }

  if (e->mode == MODE_QUICKOPEN) {
    if (ev->type == PE_KEY) {
      if (ev->key == PK_ESCAPE) {
        e->mode = MODE_EDIT;
        set_status(e, "F1 help");
        return;
      }
      if (ev->key == PK_UP) {
        if (e->qf_sel > 0) e->qf_sel--;
        mark(e);
        return;
      }
      if (ev->key == PK_DOWN) {
        if (e->qf_n && e->qf_sel + 1 < e->qf_n) e->qf_sel++;
        mark(e);
        return;
      }
      if (ev->key == PK_ENTER) {
        if (e->qf_n > 0) {
          char path[1024];
          mote_snprintf(path, sizeof path, "%s/%s", e->qf_dir,
                        e->qf_match[e->qf_sel]);
          ed_open_path(e, path);
        }
        e->mode = MODE_EDIT;
        return;
      }
    }
    if (ev->type == PE_TEXT && ev->text_len == 1 &&
        (ev->text[0] == 'j' || ev->text[0] == 'k')) {
      if (ev->text[0] == 'k' && e->qf_sel > 0) e->qf_sel--;
      if (ev->text[0] == 'j' && e->qf_n && e->qf_sel + 1 < e->qf_n) e->qf_sel++;
      mark(e);
      return;
    }
    if (ev->type == PE_TEXT && ev->text_len > 0) {
      size_t n = strlen(e->prompt);
      if (n + (size_t)ev->text_len < sizeof e->prompt - 1) {
        memcpy(e->prompt + n, ev->text, (size_t)ev->text_len);
        e->prompt[n + (size_t)ev->text_len] = 0;
        quickopen_filter(e);
        mark(e);
      }
      return;
    }
    if (ev->type == PE_KEY && ev->key == PK_BACKSPACE) {
      size_t n = strlen(e->prompt);
      if (n) {
        n = utf8_prev(e->prompt, n);
        e->prompt[n] = 0;
        quickopen_filter(e);
        mark(e);
      }
      return;
    }
    return;
  }

  if (e->mode != MODE_EDIT) {
    if (e->mode == MODE_QUITASK && ev->type == PE_KEY) {
      if (ev->key == PK_QUIT) {
        e->want_quit = MOTE_TRUE;
        return;
      }
      if (ev->key == PK_SAVE) {
        finish_quit_saves(e);
        return;
      }
      if (ev->key == PK_ESCAPE) {
        e->quit_after_save = MOTE_FALSE;
        e->mode = MODE_EDIT;
        set_status(e, "F1 help");
        return;
      }
    }
    if (e->mode == MODE_OPENASK && ev->type == PE_KEY) {
      if (ev->key == PK_QUIT) {
        d->dirty = MOTE_FALSE;
        e->mode = MODE_EDIT;
        if (e->pending_path[0]) ed_open_path(e, e->pending_path);
        e->pending_path[0] = 0;
        return;
      }
      if (ev->key == PK_SAVE) {
        if (!d->path[0]) {
          e->mode = MODE_SAVEAS;
          e->prompt[0] = 0;
          set_status(e, "Save As:");
          return;
        }
        if (!save_to(e, d, d->path)) return;
        e->mode = MODE_EDIT;
        if (e->pending_path[0]) {
          char path[1024];
          mote_snprintf(path, sizeof path, "%s", e->pending_path);
          e->pending_path[0] = 0;
          ed_open_path(e, path);
        }
        return;
      }
      if (ev->key == PK_ESCAPE) {
        e->pending_path[0] = 0;
        e->mode = MODE_EDIT;
        set_status(e, "F1 help");
        return;
      }
    }
    if (e->mode == MODE_CLOSEASK && ev->type == PE_KEY) {
      if (ev->key == PK_QUIT) {
        d->dirty = MOTE_FALSE;
        e->mode = MODE_EDIT;
        close_doc_force(e);
        return;
      }
      if (ev->key == PK_SAVE) {
        if (!d->path[0]) {
          e->close_after_save = MOTE_TRUE;
          e->mode = MODE_SAVEAS;
          e->prompt[0] = 0;
          set_status(e, "Save As:");
          return;
        }
        if (!save_to(e, d, d->path)) return;
        e->mode = MODE_EDIT;
        close_doc_force(e);
        return;
      }
      if (ev->key == PK_ESCAPE) {
        e->close_after_save = MOTE_FALSE;
        e->mode = MODE_EDIT;
        set_status(e, "F1 help");
        return;
      }
    }
    handle_prompt(e, ev);
    return;
  }

  if (ev->type == PE_SCROLL) {
    size_t top = view_vrow0(e, d);
    int step = ev->wheel;
    if (step > 0)
      top = top > (size_t)step ? top - (size_t)step : 0;
    else if (step < 0)
      top += (size_t)(-step);
    set_view_vrow(e, d, top);
    mark(e);
    return;
  }

  if (ev->type == PE_MOUSE_DOWN) {
    np = click_to_pos(e, d, ev->mx, ev->my);
    e->mouse_down = MOTE_TRUE;
    d->caret = np;
    if (!ev->shift) d->sel_anchor = d->caret;
    sync_caret_rc(d);
    ensure_visible(e, d);
    mark(e);
    return;
  }
  if (ev->type == PE_MOUSE_UP) {
    e->mouse_down = MOTE_FALSE;
    return;
  }
  if (ev->type == PE_MOUSE_MOVE && e->mouse_down) {
    d->caret = click_to_pos(e, d, ev->mx, ev->my);
    sync_caret_rc(d);
    ensure_visible(e, d);
    mark(e);
    return;
  }

  if (ev->type == PE_TEXT && ev->text_len > 0 && !ev->ctrl) {
    if (ev->text_len == 1) {
      char c = ev->text[0];
      if (c == '(') {
        insert_autoclose(e, d, '(', ')');
        return;
      }
      if (c == '[') {
        insert_autoclose(e, d, '[', ']');
        return;
      }
      if (c == '{') {
        insert_autoclose(e, d, '{', '}');
        return;
      }
      if (c == '"') {
        insert_autoclose(e, d, '"', '"');
        return;
      }
      if (c == '\'') {
        insert_autoclose(e, d, '\'', '\'');
        return;
      }
    }
    insert_text(e, d, ev->text, (size_t)ev->text_len);
    return;
  }
  if (ev->type != PE_KEY) return;

  keep = ev->shift;
  len = buf_len(&d->buf);

  switch (ev->key) {
  case PK_LEFT:
    if (ev->ctrl) move_caret(e, d, prev_word(d, d->caret), keep);
    else move_caret(e, d, ed_prev(d, d->caret), keep);
    break;
  case PK_RIGHT:
    if (ev->ctrl) move_caret(e, d, next_word(d, d->caret), keep);
    else move_caret(e, d, ed_next(d, d->caret), keep);
    break;
  case PK_UP: move_vert(e, d, -1, keep); break;
  case PK_DOWN: move_vert(e, d, 1, keep); break;
  case PK_HOME: move_caret(e, d, line_start(d, d->caret), keep); break;
  case PK_END: move_caret(e, d, line_end(d, d->caret), keep); break;
  case PK_PGUP: move_vert(e, d, -(e->rows > 1 ? e->rows - 1 : 1), keep); break;
  case PK_PGDN: move_vert(e, d, e->rows > 1 ? e->rows - 1 : 1, keep); break;
  case PK_BACKSPACE:
    if (!can_edit(d)) {
      set_status(e, "readonly");
      break;
    }
    if (has_sel(d)) delete_sel(e, d);
    else if (d->caret > 0) {
      np = ed_prev(d, d->caret);
      push_delete(e, d, np, d->caret - np);
      d->caret = np;
      clear_sel(d);
      sync_caret_rc(d);
    }
    ensure_visible(e, d);
    mark(e);
    break;
  case PK_DELETE:
    if (!can_edit(d)) {
      set_status(e, "readonly");
      break;
    }
    if (has_sel(d)) delete_sel(e, d);
    else if (d->caret < len) {
      np = ed_next(d, d->caret);
      push_delete(e, d, d->caret, np - d->caret);
      clear_sel(d);
      sync_caret_rc(d);
    }
    mark(e);
    break;
  case PK_ENTER: insert_newline_indent(e, d); break;
  case PK_TAB:
    if (ev->shift) indent_sel(e, d, -1);
    else indent_sel(e, d, 1);
    break;
  case PK_SAVE: try_save(e, d); break;
  case PK_SAVEAS:
    e->mode = MODE_SAVEAS;
    e->prompt[0] = 0;
    set_status(e, "Save As:");
    break;
  case PK_OPEN:
    e->mode = MODE_OPEN;
    e->prompt[0] = 0;
    set_status(e, "Open:");
    break;
  case PK_QUIT: request_quit(e); break;
  case PK_UNDO: do_undo(e, d); break;
  case PK_REDO: do_redo(e, d); break;
  case PK_FIND:
    if (e->mode == MODE_FIND) {
      mark(e);
      break;
    }
    e->mode = MODE_FIND;
    mote_snprintf(e->prompt, sizeof e->prompt, "%s", e->find);
    d->match_a = d->match_b = 0;
    set_status(e, "Find: /re/ or text  Alt+C case  Alt+W word");
    break;
  case PK_FINDNEXT: find_next(e, d); mark(e); break;
  case PK_FINDPREV: find_prev(e, d); mark(e); break;
  case PK_FINDCASE:
    e->find_case = !e->find_case;
    set_status(e, e->find_case ? "find: case on" : "find: case off");
    break;
  case PK_FINDWORD:
    e->find_word = !e->find_word;
    set_status(e, e->find_word ? "find: word on" : "find: word off");
    break;
  case PK_REPLACE:
    e->mode = MODE_REPLACE;
    e->prompt[0] = 0;
    set_status(e, "Replace: /find/repl/ or text");
    break;
  case PK_GOTO:
    e->mode = MODE_GOTO;
    e->prompt[0] = 0;
    set_status(e, "Goto:");
    break;
  case PK_THEME: cycle_theme(e); break;
  case PK_CUT: cut_sel(e, d, p); break;
  case PK_COPY: copy_sel(e, d, p); break;
  case PK_PASTE: paste_clip(e, d, p); break;
  case PK_SELALL:
    d->sel_anchor = 0;
    d->caret = len;
    sync_caret_rc(d);
    mark(e);
    break;
  case PK_HELP:
  case PK_F1:
    e->mode = MODE_HELP;
    mark(e);
    break;
  case PK_WRAP:
    e->wrap = !e->wrap;
    free(e->vrow_cache);
    e->vrow_cache = NULL;
    e->vrow_n = 0;
    if (e->wrap) {
      d->col0 = 0;
      d->wrap0 = 0;
    }
    ensure_visible(e, d);
    set_status(e, e->wrap ? "wrap on" : "wrap off");
    break;
  case PK_WS:
    e->show_ws = !e->show_ws;
    set_status(e, e->show_ws ? "whitespace on" : "whitespace off");
    break;
  case PK_DELLINE: delete_line(e, d); break;
  case PK_DUPLINE: dup_line(e, d); break;
  case PK_ZOOMIN: {
    int px = plat_font_px(p) + 1;
    plat_set_font_px(p, px);
    set_status(e, "zoom+");
    mark(e);
    break;
  }
  case PK_ZOOMOUT: {
    int px = plat_font_px(p) - 1;
    plat_set_font_px(p, px);
    set_status(e, "zoom-");
    mark(e);
    break;
  }
  case PK_ZOOMRESET:
    plat_set_font_px(p, 15);
    set_status(e, "zoom reset");
    mark(e);
    break;
  case PK_NEWDOC: ed_new_doc(e); break;
  case PK_NEXTDOC: switch_doc(e, (e->cur + 1) % e->ndocs); break;
  case PK_PREVDOC:
    switch_doc(e, (e->cur + e->ndocs - 1) % e->ndocs);
    break;
  case PK_RELOAD: reload_doc(e, d); break;
  case PK_READONLY:
    d->readonly = !d->readonly;
    set_status(e, d->readonly ? "readonly on" : "readonly off");
    break;
  case PK_EOL:
    d->eol = d->eol == EOL_LF ? EOL_CRLF : EOL_LF;
    d->dirty = MOTE_TRUE;
    set_status(e, d->eol == EOL_CRLF ? "eol CRLF" : "eol LF");
    break;
  case PK_CLOSEDOC: close_doc(e); break;
  case PK_BRACKET: jump_bracket(e, d); break;
  case PK_RECENT:
    if (!e->nrecent) {
      set_status(e, "no recent");
      break;
    }
    e->mode = MODE_RECENT;
    e->recent_sel = 0;
    set_status(e, "Recent — j/k Enter, 1-8");
    mark(e);
    break;
  case PK_QUICKOPEN:
    quickopen_begin(e, d);
    break;
  case PK_COMMENT:
    toggle_comment(e, d);
    break;
  case PK_BOOKMARK_SET:
    bookmark_toggle(e, d);
    break;
  case PK_BOOKMARK:
    bookmark_jump(e, d);
    break;
  default:
    break;
  }
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

void ed_draw(Editor *e, Plat *p) {
  int w, h, i, sw, digits;
  size_t crow, ccol, a, b, nlines, lrow, wseg, col0, col_max;
  char bar[384], num[16];
  const char *name;
  Doc *d = D(e);
  const Theme *t = th(e);
  const HlSyntax *syn = hl_select(d->path);
  int in_ml = 0;
  size_t cvr, cvc, top;
  size_t *vrow_vp = NULL;
  static const char *help[] = {
      "mote  F1/Alt+H help  Esc",
      "File  ^S Alt+S ^O ^P ^Q  F5  ^N  ^W  ^F4/S-W  ^E  F2/^Tab",
      "Edit  ^Z/^Y  ^F F3/S-F3  ^R  ^G  ^/  ^D  ^]  Alt+K  Tab",
      "Nav   ^B jump  F8/Alt+B set  F9/Alt+J  ^M xterm  ^P quick open",
      "Clip  ^X/^C/^V/^A",
      "View  ^T  F7  ^=/^-/^0  Alt+R  Alt+E",
      "Find  /pat/ regex  /a/b/ replace  Alt+C case  Alt+W word",
      "Ask   ^S confirm  ^Q discard  Esc",
  };
  plat_get_size(p, &w, &h);
  e->cw = plat_font_w(p);
  e->ch = plat_font_h(p);
  if (e->cw < 1) e->cw = 8;
  if (e->ch < 1) e->ch = 16;

  ensure_lines(d);
  sync_caret_rc(d);
  nlines = d->lines.n ? d->lines.n : 1;
  digits = 1;
  {
    size_t tln = nlines;
    while (tln >= 10) {
      digits++;
      tln /= 10;
    }
  }
  e->gutter = (digits + 1) * e->cw;
  if (e->gutter > w / 3) e->gutter = w / 3;
  e->cols = (w - e->gutter) / e->cw;
  /* Status strip pinned to bottom; leftover h%ch stays inside the strip. */
  {
    int status_h = e->ch;
    if (status_h < 1) status_h = 1;
    if (status_h > h) status_h = h;
    e->rows = (h - status_h) / e->ch;
  }
  if (e->cols < 1) e->cols = 1;
  if (e->rows < 1) e->rows = 1;
  if (e->wrap) d->col0 = 0;
  if (e->wrap && e->cols > 0) {
    size_t segs = segs_of(e, d, d->row0);
    if (segs < 1) segs = 1;
    if (d->wrap0 >= segs) d->wrap0 = segs - 1;
  }

  vrow_vp = NULL;
  if (e->wrap && e->cols > 0 && nlines > 0) {
    if (!e->vrow_cache || e->vrow_n != nlines || e->vrow_cols != e->cols ||
        d->lines.dirty) {
      free(e->vrow_cache);
      e->vrow_cache = build_vrow_prefix(e, d, nlines);
      e->vrow_n = nlines;
      e->vrow_cols = e->cols;
    }
    vrow_vp = e->vrow_cache;
  }

  if (e->mode == MODE_EDIT) find_bracket(d);

  plat_begin_frame(p);
  plat_clear(p, t->bg);
  if (e->gutter > 0)
    plat_fill_rect(p, 0, 0, e->gutter, h - e->ch > 0 ? h - e->ch : 0, t->gutter_bg);

  if (syn && hl_has_multiline(syn)) {
    size_t r, r0, ra, rb;
    char linebuf[4096];
    int ncopy;
    in_ml = 0;
    if (d->hl_ml_valid && d->hl_ml_row <= d->row0) {
      r0 = d->hl_ml_row;
      in_ml = d->hl_in_ml;
    } else {
      r0 = d->row0 > 128 ? d->row0 - 128 : 0;
      d->hl_ml_valid = MOTE_FALSE;
    }
    for (r = r0; r < d->row0; r++) {
      ra = row_start(d, r);
      rb = line_end(d, ra);
      ncopy = (int)(rb - ra);
      if (ncopy > (int)sizeof linebuf - 1) ncopy = (int)sizeof linebuf - 1;
      for (i = 0; i < ncopy; i++) linebuf[i] = buf_at(&d->buf, ra + (size_t)i);
      linebuf[ncopy] = 0;
      hl_line(syn, linebuf, (size_t)ncopy, in_ml, NULL, 0, &in_ml);
    }
    d->hl_in_ml = in_ml;
    d->hl_ml_row = d->row0;
    d->hl_ml_valid = MOTE_TRUE;
  }

  if (!d->row0_valid) {
    d->row0_pos = row_start(d, d->row0);
    d->row0_valid = MOTE_TRUE;
  }
  lrow = d->row0;
  wseg = d->wrap0;
  for (i = 0; i < e->rows; i++) {
    HlSpan spans[HL_MAX_SPANS];
    int nspans = 0;
    char linebuf[4096];
    int ncopy;
    size_t segs;
    if (lrow >= nlines) break;
    a = row_start(d, lrow);
    b = line_end(d, a);
    segs = segs_of(e, d, lrow);
    if (wseg >= segs) {
      wseg = 0;
      lrow++;
      continue;
    }
    col0 = e->wrap ? wseg * (size_t)e->cols : d->col0;
    col_max = e->wrap ? col0 + (size_t)e->cols : d->col0 + (size_t)e->cols;
    if (lrow == d->caret_row)
      plat_fill_rect(p, e->gutter, i * e->ch, w - e->gutter, e->ch, t->line);
    if (wseg == 0) {
      int nlen, nx, bi;
      mote_bool has_bm = MOTE_FALSE;
      for (bi = 0; bi < MAX_BOOKMARKS; bi++)
        if (d->bm_row[bi] != (size_t)-1 && d->bm_row[bi] == lrow) {
          has_bm = MOTE_TRUE;
          break;
        }
      mote_snprintf(num, sizeof num, "%*lu%c", digits, (unsigned long)(lrow + 1),
                    has_bm ? '*' : ' ');
      nlen = (int)strlen(num);
      /* right-align in gutter with one cell/gap before text */
      nx = e->gutter - (nlen + 1) * e->cw;
      if (nx < 0) nx = 0;
      plat_draw_text(p, nx, i * e->ch, num, nlen, t->gutter_fg);
    }
    ncopy = (int)(b - a);
    if (ncopy > (int)sizeof linebuf - 1) ncopy = (int)sizeof linebuf - 1;
    {
      int k;
      for (k = 0; k < ncopy; k++) linebuf[k] = buf_at(&d->buf, a + (size_t)k);
    }
    linebuf[ncopy] = 0;
    if (syn) {
      int ml = in_ml;
      nspans =
          hl_line(syn, linebuf, (size_t)ncopy, ml, spans, HL_MAX_SPANS, &ml);
      if (wseg + 1 >= segs) in_ml = ml;
    }
    draw_range(e, d, p, a, b, i * e->ch, col0, col_max, spans, nspans, t);
    wseg++;
    if (wseg >= segs) {
      wseg = 0;
      lrow++;
    }
  }

  crow = d->caret_row;
  ccol = d->caret_col;
  caret_vis_vp(e, d, &cvr, &cvc, vrow_vp);
  top = view_vrow0_vp(e, d, vrow_vp);
  if (e->mode != MODE_HELP && e->mode != MODE_RECENT && e->mode != MODE_QUICKOPEN &&
      cvr >= top &&
      cvr < top + (size_t)e->rows) {
    int cx, cy;
    size_t sc = e->wrap ? cvc : (ccol >= d->col0 ? ccol - d->col0 : 0);
    int ok = e->wrap || (ccol >= d->col0 && ccol < d->col0 + (size_t)e->cols);
    if (ok) {
      cx = e->gutter + (int)sc * e->cw;
      cy = (int)(cvr - top) * e->ch;
      if (!plat_set_caret(p, cx, cy, e->ch, MOTE_TRUE))
        plat_fill_rect(p, cx, cy, e->cw > 0 ? e->cw : 1, e->ch, t->caret);
    } else
      plat_set_caret(p, 0, 0, e->ch, MOTE_FALSE);
  } else {
    plat_set_caret(p, 0, 0, e->ch, MOTE_FALSE);
  }

  sw = h - e->ch;
  if (sw < 0) sw = 0;
  plat_fill_rect(p, 0, sw, w, h - sw, t->status);
  name = d->path[0] ? d->path : "[untitled]";
  if (e->mode == MODE_OPEN || e->mode == MODE_SAVEAS || e->mode == MODE_FIND ||
      e->mode == MODE_REPLACE || e->mode == MODE_GOTO || e->mode == MODE_QUICKOPEN) {
    const char *pfx = prompt_bar_prefix(e->mode);
    if (e->prompt[0])
      mote_snprintf(bar, sizeof bar, "%s %s", pfx, e->prompt);
    else
      mote_snprintf(bar, sizeof bar, "%s", pfx);
  }
  else if (e->mode == MODE_QUITASK || e->mode == MODE_OPENASK ||
           e->mode == MODE_CLOSEASK)
    mote_snprintf(bar, sizeof bar, "%s", e->status);
  else {
    const char *base = path_base(name);
    mote_snprintf(bar, sizeof bar, "[%d/%d] %s%s%s  %lu:%lu  %s  %s  %s", e->cur + 1,
             e->ndocs, base, d->dirty ? "*" : "", d->readonly ? " RO" : "",
             (unsigned long)(crow + 1), (unsigned long)(ccol + 1),
             d->eol == EOL_CRLF ? "CRLF" : "LF",
             hl_lang_name(syn), e->status[0] ? e->status : "F1 help");
  }
  /* Full-width status text (pad 1 cell when space allows). */
  {
    int pad = (e->cw > 0 && w > e->cw * 2) ? e->cw : 0;
    draw_text_fit(p, pad, sw, bar, (int)strlen(bar), t->status_fg, w - pad,
                  e->cw > 0 ? e->cw : 1);
  }

  if (e->mode == MODE_HELP || e->mode == MODE_RECENT || e->mode == MODE_QUICKOPEN) {
    int nlines_h, box_h, box_y, box_w, max_h, max_lines, draw_n, text_w;
    int mx, tx, pad, rows_fit;
    pad = e->cw > 0 ? e->cw : 1;
    mx = pad * 2;
    if (mx * 2 >= w) mx = pad;
    if (mx < 1) mx = 1;
    tx = mx + pad;
    if (tx >= w - pad) tx = mx;
    box_y = pad * 2;
    if (box_y < e->ch) box_y = e->ch;
    box_w = w - mx * 2;
    if (box_w < pad * 4) box_w = w > 2 ? w - 2 : w;
    if (box_w < 1) box_w = 1;
    /* inner text width: left inset (tx-mx) + right pad cell */
    text_w = box_w - (tx - mx) - pad;
    if (text_w < e->cw) text_w = box_w - (tx - mx);
    if (text_w < 1) text_w = 1;
    max_h = sw - box_y;
    if (max_h < e->ch) max_h = e->ch;
    rows_fit = e->ch > 0 ? max_h / e->ch : 1;
    max_lines = rows_fit > 2 ? rows_fit - 2 : 1;
    if (e->mode == MODE_HELP)
      nlines_h = (int)(sizeof help / sizeof help[0]);
    else if (e->mode == MODE_RECENT)
      nlines_h = e->nrecent + 1;
    else
      nlines_h = e->qf_n + 1;
    draw_n = nlines_h;
    if (draw_n > max_lines) draw_n = max_lines;
    box_h = (draw_n + 2) * e->ch;
    if (box_h > max_h) {
      box_h = max_h - (max_h % (e->ch > 0 ? e->ch : 1));
      if (box_h < e->ch) box_h = e->ch;
      draw_n = e->ch > 0 ? box_h / e->ch - 2 : 1;
      if (draw_n < 1) draw_n = 1;
    }
    {
      int inset = pad > 1 ? pad / 2 : (pad > 0 ? 1 : 0);
      int bx = mx > inset ? mx - inset : 0;
      int by = box_y > inset ? box_y - inset : 0;
      plat_fill_rect(p, bx, by, box_w + inset * 2, box_h + inset * 2, t->help_bd);
    }
    plat_fill_rect(p, mx, box_y, box_w, box_h, t->help_bg);
    if (e->mode == MODE_HELP) {
      for (i = 0; i < draw_n; i++) {
        int ly = box_y + (i + 1) * e->ch;
        if (ly + e->ch > box_y + box_h) break;
        draw_text_fit(p, tx, ly, help[i], (int)strlen(help[i]), t->fg, text_w,
                      e->cw);
      }
      if (draw_n < nlines_h) {
        int ly = box_y + (draw_n + 1) * e->ch;
        if (ly + e->ch <= box_y + box_h)
          draw_text_fit(p, tx, ly, "  ... Esc", 9, t->gutter_fg, text_w, e->cw);
      }
    } else if (e->mode == MODE_RECENT) {
      draw_text_fit(p, tx, box_y + e->ch, "Recent", 6, t->fg, text_w, e->cw);
      for (i = 0; i < e->nrecent && (i + 1) < draw_n; i++) {
        char line[300];
        int ly = box_y + (i + 2) * e->ch;
        if (ly + e->ch > box_y + box_h) break;
        mote_snprintf(line, sizeof line, "%s%d %s",
                      i == e->recent_sel ? "> " : "  ", i + 1,
                      path_base(e->recent[i]));
        draw_text_fit(p, tx, ly, line, (int)strlen(line),
                      i == e->recent_sel ? t->kw : t->fg, text_w, e->cw);
      }
    } else {
      draw_text_fit(p, tx, box_y + e->ch, "Go to file", 10, t->fg, text_w, e->cw);
      for (i = 0; i < e->qf_n && (i + 1) < draw_n; i++) {
        char line[300];
        int ly = box_y + (i + 2) * e->ch;
        if (ly + e->ch > box_y + box_h) break;
        mote_snprintf(line, sizeof line, "%s%s",
                      i == e->qf_sel ? "> " : "  ", e->qf_match[i]);
        draw_text_fit(p, tx, ly, line, (int)strlen(line),
                      i == e->qf_sel ? t->kw : t->fg, text_w, e->cw);
      }
    }
  }

  {
    static char last[260];
    char title[260];
    const char *base = path_base(d->path[0] ? d->path : "untitled");
    mote_snprintf(title, sizeof title, "%s%s — mote", d->dirty ? "*" : "", base);
    if (strcmp(title, last) != 0) {
      mote_snprintf(last, sizeof last, "%s", title);
      plat_set_title(p, title);
    }
  }

  plat_end_frame(p);
  e->need_draw = MOTE_FALSE;
}
