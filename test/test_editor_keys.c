/* Headless editor functional test: PlatKeys + save roundtrip. */
#include "editor.h"
#include "buffer.h"
#include "common.h"
#include "mote_snprintf.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int fails;

static void expect(int cond, const char *msg) {
  if (!cond) {
    fprintf(stderr, "FAIL: %s\n", msg);
    fails++;
  }
}

struct Plat {
  int w, h, font_px;
  char *clip;
  size_t clip_n;
  PlatEvent q[64];
  int qn;
};

Plat *plat_create(const char *title, int w, int h) {
  Plat *p = (Plat *)calloc(1, sizeof *p);
  (void)title;
  if (!p) return NULL;
  p->w = w > 0 ? w : 80;
  p->h = h > 0 ? h : 25;
  p->font_px = 15;
  return p;
}
void plat_destroy(Plat *p) {
  if (!p) return;
  free(p->clip);
  free(p);
}
void plat_wait(Plat *p) { (void)p; }
mote_bool plat_poll(Plat *p, PlatEvent *ev) {
  if (p->qn <= 0) return MOTE_FALSE;
  *ev = p->q[0];
  p->qn--;
  memmove(p->q, p->q + 1, (size_t)p->qn * sizeof p->q[0]);
  return MOTE_TRUE;
}
void plat_get_size(Plat *p, int *w, int *h) {
  if (w) *w = p->w;
  if (h) *h = p->h;
}
int plat_font_w(Plat *p) {
  (void)p;
  return 1;
}
int plat_font_h(Plat *p) {
  (void)p;
  return 1;
}
void plat_set_font_px(Plat *p, int px) { p->font_px = px; }
int plat_font_px(Plat *p) { return p->font_px; }
void plat_begin_frame(Plat *p) { (void)p; }
void plat_clear(Plat *p, mote_u32 rgb) {
  (void)p;
  (void)rgb;
}
void plat_fill_rect(Plat *p, int x, int y, int w, int h, mote_u32 rgb) {
  (void)p;
  (void)x;
  (void)y;
  (void)w;
  (void)h;
  (void)rgb;
}
void plat_draw_text(Plat *p, int x, int y, const char *s, int n, mote_u32 rgb) {
  (void)p;
  (void)x;
  (void)y;
  (void)s;
  (void)n;
  (void)rgb;
}
void plat_end_frame(Plat *p) { (void)p; }
void plat_set_title(Plat *p, const char *title) {
  (void)p;
  (void)title;
}
mote_bool plat_set_caret(Plat *p, int x, int y, int h, mote_bool on) {
  (void)p;
  (void)x;
  (void)y;
  (void)h;
  (void)on;
  return MOTE_TRUE;
}
char *plat_clipboard_get(Plat *p, size_t *out_len) {
  char *d;
  if (!p->clip || !p->clip_n) {
    if (out_len) *out_len = 0;
    return NULL;
  }
  d = (char *)malloc(p->clip_n + 1);
  if (!d) return NULL;
  memcpy(d, p->clip, p->clip_n);
  d[p->clip_n] = 0;
  if (out_len) *out_len = p->clip_n;
  return d;
}
mote_bool plat_clipboard_set(Plat *p, const char *s, size_t n) {
  char *d = (char *)malloc(n + 1);
  if (!d) return MOTE_FALSE;
  memcpy(d, s, n);
  d[n] = 0;
  free(p->clip);
  p->clip = d;
  p->clip_n = n;
  return MOTE_TRUE;
}

FILE *plat_fopen(const char *path, const char *mode) { return fopen(path, mode); }
int plat_remove(const char *path) { return remove(path); }
int plat_rename(const char *from, const char *to) { return rename(from, to); }
void plat_fsync_file(FILE *f) {
  if (f) fflush(f);
}
int plat_config_path(char *out, size_t n) {
  return mote_snprintf(out, n, "/tmp/mote-test-config") >= (int)n ? -1 : 0;
}

static void push_key(Plat *p, PlatKey k, mote_bool ctrl, mote_bool shift) {
  PlatEvent e;
  memset(&e, 0, sizeof e);
  e.type = PE_KEY;
  e.key = k;
  e.ctrl = ctrl;
  e.shift = shift;
  if (p->qn < 64) p->q[p->qn++] = e;
}
static void push_text(Plat *p, const char *s) {
  PlatEvent e;
  memset(&e, 0, sizeof e);
  e.type = PE_TEXT;
  e.text_len = (int)strlen(s);
  if (e.text_len > (int)sizeof e.text) e.text_len = (int)sizeof e.text;
  memcpy(e.text, s, (size_t)e.text_len);
  if (p->qn < 64) p->q[p->qn++] = e;
}
static void drain(Editor *ed, Plat *p) {
  PlatEvent ev;
  while (plat_poll(p, &ev)) ed_handle(ed, p, &ev);
  if (ed->need_draw) ed_draw(ed, p);
}
static char *doc_str(Editor *ed) { return buf_strdup(&ed->docs[ed->cur].buf); }

int main(void) {
  Plat *p;
  Editor ed;
  char *s;
  const char *path = "/tmp/mote-keytest-out.c";
  const char *path2 = "/tmp/mote-keytest-as.c";
  fails = 0;
  unlink(path);
  unlink(path2);

  p = plat_create("t", 80, 25);
  expect(p != NULL, "plat_create");
  expect(ed_init(&ed), "ed_init");
  ed_draw(&ed, p);

  push_text(p, "int x = 42;\n");
  drain(&ed, p);
  s = doc_str(&ed);
  expect(s && strstr(s, "42"), "typed text");
  free(s);

  mote_snprintf(ed.docs[ed.cur].path, sizeof ed.docs[0].path, "%s", path);
  push_key(p, PK_SAVE, MOTE_TRUE, MOTE_FALSE);
  drain(&ed, p);
  {
    FILE *f = fopen(path, "r");
    char buf[64];
    memset(buf, 0, sizeof buf);
    expect(f != NULL, "save created file");
    if (f) {
      expect(fread(buf, 1, sizeof buf - 1, f) > 0, "save non-empty");
      expect(strstr(buf, "42") != NULL, "save contents");
      fclose(f);
    }
  }

  push_text(p, "ZZZ");
  drain(&ed, p);
  push_key(p, PK_UNDO, MOTE_TRUE, MOTE_FALSE);
  drain(&ed, p);
  s = doc_str(&ed);
  expect(s && !strstr(s, "ZZZ"), "after undo");
  free(s);
  push_key(p, PK_REDO, MOTE_TRUE, MOTE_FALSE);
  drain(&ed, p);
  s = doc_str(&ed);
  expect(s && strstr(s, "ZZZ"), "after redo");
  free(s);
  push_key(p, PK_UNDO, MOTE_TRUE, MOTE_FALSE);
  drain(&ed, p);

  /* seed clipboard directly then paste — also test cut via explicit set */
  expect(plat_clipboard_set(p, "int x = 42;\n", 12), "clip set");
  push_key(p, PK_SELALL, MOTE_TRUE, MOTE_FALSE);
  drain(&ed, p);
  push_key(p, PK_DELETE, MOTE_FALSE, MOTE_FALSE);
  drain(&ed, p);
  push_key(p, PK_PASTE, MOTE_TRUE, MOTE_FALSE);
  drain(&ed, p);
  s = doc_str(&ed);
  expect(s && strstr(s, "42"), "paste restored");
  free(s);

  /* copy selection into clipboard */
  push_key(p, PK_SELALL, MOTE_TRUE, MOTE_FALSE);
  drain(&ed, p);
  push_key(p, PK_COPY, MOTE_TRUE, MOTE_FALSE);
  drain(&ed, p);
  {
    size_t cn = 0;
    char *clip = plat_clipboard_get(p, &cn);
    expect(clip && cn >= 2 && strstr(clip, "42"), "copy to clipboard");
    free(clip);
  }

  {
    size_t before = buf_len(&ed.docs[ed.cur].buf);
    push_key(p, PK_HOME, MOTE_FALSE, MOTE_FALSE);
    drain(&ed, p);
    push_key(p, PK_DUPLINE, MOTE_TRUE, MOTE_FALSE);
    drain(&ed, p);
    expect(buf_len(&ed.docs[ed.cur].buf) > before, "dupline grew");
  }

  {
    int t0 = ed.theme_id;
    push_key(p, PK_THEME, MOTE_TRUE, MOTE_FALSE);
    drain(&ed, p);
    expect(ed.theme_id != t0, "theme changed");
  }

  push_key(p, PK_WRAP, MOTE_TRUE, MOTE_FALSE);
  drain(&ed, p);
  expect(ed.wrap, "wrap on");
  push_key(p, PK_WS, MOTE_FALSE, MOTE_FALSE);
  drain(&ed, p);
  expect(ed.show_ws, "ws on");
  push_key(p, PK_EOL, MOTE_TRUE, MOTE_TRUE);
  drain(&ed, p);
  expect(ed.docs[ed.cur].eol == EOL_CRLF, "eol crlf");
  push_key(p, PK_READONLY, MOTE_TRUE, MOTE_TRUE);
  drain(&ed, p);
  expect(ed.docs[ed.cur].readonly, "readonly on");
  push_key(p, PK_READONLY, MOTE_TRUE, MOTE_TRUE);
  drain(&ed, p);

  push_key(p, PK_NEWDOC, MOTE_TRUE, MOTE_FALSE);
  drain(&ed, p);
  expect(ed.ndocs == 2, "new doc");
  push_key(p, PK_NEXTDOC, MOTE_TRUE, MOTE_FALSE);
  drain(&ed, p);
  push_key(p, PK_PREVDOC, MOTE_TRUE, MOTE_TRUE);
  drain(&ed, p);

  mote_snprintf(ed.find, sizeof ed.find, "%s", "42");
  push_key(p, PK_FINDNEXT, MOTE_FALSE, MOTE_FALSE);
  drain(&ed, p);

  push_key(p, PK_DELLINE, MOTE_TRUE, MOTE_TRUE);
  drain(&ed, p);

  push_text(p, "  ab");
  drain(&ed, p);
  push_key(p, PK_ENTER, MOTE_FALSE, MOTE_FALSE);
  drain(&ed, p);
  s = doc_str(&ed);
  expect(s && strstr(s, "\n  "), "autoindent");
  free(s);

  mote_snprintf(ed.docs[ed.cur].path, sizeof ed.docs[0].path, "%s", path2);
  ed.docs[ed.cur].dirty = MOTE_TRUE;
  ed.docs[ed.cur].readonly = MOTE_FALSE;
  push_key(p, PK_SAVE, MOTE_TRUE, MOTE_FALSE);
  drain(&ed, p);
  expect(access(path2, R_OK) == 0, "second save path written");

  {
    static const PlatKey allk[] = {
        PK_LEFT,     PK_RIGHT,     PK_UP,        PK_DOWN,      PK_HOME,
        PK_END,      PK_PGUP,      PK_PGDN,      PK_BACKSPACE, PK_ESCAPE,
        PK_TAB,      PK_F1,        PK_OPEN,      PK_GOTO,      PK_REPLACE,
        PK_FIND,     PK_FINDPREV,  PK_FINDCASE,  PK_FINDWORD,  PK_BRACKET,
        PK_ZOOMIN,   PK_ZOOMOUT,   PK_ZOOMRESET, PK_RELOAD,    PK_RECENT,
        PK_HELP,     PK_CLOSEDOC,  PK_SAVEAS};
    size_t i;
    for (i = 0; i < sizeof allk / sizeof allk[0]; i++) {
      push_key(p, allk[i], MOTE_FALSE, MOTE_FALSE);
      drain(&ed, p);
      if (ed.mode != MODE_EDIT) {
        push_key(p, PK_ESCAPE, MOTE_FALSE, MOTE_FALSE);
        drain(&ed, p);
      }
    }
  }

  ed_free(&ed);
  plat_destroy(p);
  unlink(path);
  unlink(path2);

  if (fails) {
    fprintf(stderr, "%d failures\n", fails);
    return 1;
  }
  puts("editor key/function matrix OK");
  return 0;
}
