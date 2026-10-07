/* mote overlay/dos — VGA text mode (DJGPP / FreeDOS) */
#include "platform.h"
#include "common.h"
#include "keymap.h"
#include "utf8.h"
#include "../evq.h"

#include <bios.h>
#include <dpmi.h>
#include <go32.h>
#include <pc.h>
#include <keys.h>
#include <sys/farptr.h>
#include <sys/movedata.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

#include "font_cp866.inc"

/* Mode 03h is always 80x25 in VRAM; a smaller -g uses its top-left corner. */
#define VGA_COLS 80
#define VGA_ROWS 25
#define VGA_TEXT 0xB8000UL

/* VGA text cell: char + attribute */
typedef struct {
  unsigned char ch;
  unsigned char attr;
  mote_u32 fg, bg; /* last RGB asked (for attr remap) */
} Cell;

struct Plat {
  int cols, rows, font_px, caret_x, caret_y;
  mote_bool caret_on;
  Cell *cells;
  Cell *prev; /* for dirty redraw */
  char *clip;
  size_t clip_n;
  EvQueue q;
};

/* Curated 16-color palette tuned for mote themes (programmed into VGA DAC). */
static const mote_u32 VGA16[16] = {
    0x0F1419ul, /* 0  bg dark */
    0x007ACCul, /* 1  status blue */
    0x6A9955ul, /* 2  comment green */
    0x4EC9B0ul, /* 3  type cyan */
    0xCE9178ul, /* 4  string warm */
    0xC586C0ul, /* 5  keyword purple */
    0xD7BA7Dul, /* 6  soft yellow / amber */
    0xF5F5F5ul, /* 7  light paper + dark-theme fg */
    0x5C6773ul, /* 8  gutter */
    0x569CD6ul, /* 9  keyword blue */
    0xD7BA7Dul, /* 10 number amber */
    0x39BAE6ul, /* 11 bright cyan */
    0xF44747ul, /* 12 bright red */
    0xD4BFFFul, /* 13 bright magenta */
    0xFF8F40ul, /* 14 help/accent orange */
    0xFFFFFFul, /* 15 white */
};

static void vga_set_dac(unsigned idx, mote_u32 rgb) {
  outportb(0x3C8, (unsigned char)(idx & 0xFFu));
  outportb(0x3C9, (unsigned char)(((rgb >> 16) & 255) >> 2));
  outportb(0x3C9, (unsigned char)(((rgb >> 8) & 255) >> 2));
  outportb(0x3C9, (unsigned char)((rgb & 255) >> 2));
}

static void vga_load_palette(void) {
  unsigned i;
  /* Mode 03h Attribute Controller maps attr N → random DAC slots in the
   * first 64 (e.g. bright green attr 10 → DAC 0x12). Force identity so
   * attr N uses DAC N, then program DAC 0..15 to our theme colors. */
  (void)inportb(0x3DA); /* reset AC address flip-flop */
  for (i = 0; i < 16; i++) {
    outportb(0x3C0, (unsigned char)i);
    outportb(0x3C0, (unsigned char)i);
  }
  (void)inportb(0x3DA);
  outportb(0x3C0, 0x20); /* PAS: enable display */
  for (i = 0; i < 16; i++) vga_set_dac(i, VGA16[i]);
}

static unsigned char cp_to_dos(mote_u32 cp) {
  /* CP866 (OEM Russian) — FreeDOS / DOSBox often use this for Cyrillic. */
  if (cp < 128) return (unsigned char)cp;
  if (cp >= 0x0410 && cp <= 0x042F) /* А-Я */
    return (unsigned char)(0x80 + (cp - 0x0410));
  if (cp >= 0x0430 && cp <= 0x043F) /* а-п */
    return (unsigned char)(0xA0 + (cp - 0x0430));
  if (cp >= 0x0440 && cp <= 0x044F) /* р-я */
    return (unsigned char)(0xE0 + (cp - 0x0440));
  if (cp == 0x0401) return 0xF0; /* Ё */
  if (cp == 0x0451) return 0xF1; /* ё */
  if (cp == 0x00B7) return 250; /* · */
  if (cp == 0x00BB) return 175; /* » */
  if (cp == 0x00AB) return 174; /* « */
  if (cp == 0x2014 || cp == 0x2013) return 196; /* — – */
  if (cp == 0x2026) return 250; /* … */
  if (cp == 0x2192) return 26;  /* → */
  if (cp == 0x2500) return 196; /* ─ */
  if (cp == 0x2502) return 179; /* │ */
  if (cp == 0x250C) return 218;
  if (cp == 0x2510) return 191;
  if (cp == 0x2514) return 192;
  if (cp == 0x2518) return 217;
  if (cp == 0x2550) return 205;
  if (cp == 0x2551) return 186;
  return '?';
}

/* Snap theme HL RGBs onto curated VGA indices so keywords don't collapse. */
static unsigned char nearest_vga(mote_u32 rgb) {
  int i, best = 0;
  long best_d = 0x7fffffffL;
  int r = (int)((rgb >> 16) & 255);
  int g = (int)((rgb >> 8) & 255);
  int b = (int)(rgb & 255);
  int bri = r + g + b;
  /* Exact / near-exact theme anchors → fixed slots (see VGA16). */
  if (r < 40 && g < 40 && b < 45) return 0;           /* editor / slate bg */
  if (bri > 40 && bri < 140 && r < 55 && g < 60 && b < 70 && !(r < 40 && g < 40))
    return 8; /* gutter / panel — only when not pure bg */
  if (r > 200 && g > 200 && b > 200) return 15;       /* white */
  if (r > 180 && g > 180 && b > 180) return 7;        /* fg */
  /* Teal/cyan before olive — type 4EC9B0 used to collapse into comment. */
  if (g > 140 && b > 140 && r < 130 && b + 40 >= g) return 3; /* type cyan */
  if (b > 170 && r < 130 && g > 100 && g < 210) return 9;     /* keyword blue */
  if (b > r + 30 && b > g && r < 120) return 1;               /* status blue */
  if (g > r + 20 && g > b + 20 && g > 100 && r < 160 && b < 130)
    return 2; /* comment olive */
  if (r > 160 && g > 100 && g < 180 && b < 140) return 4; /* string */
  if (r > 150 && b > 150 && g < 160) return 5;        /* keyword purple */
  if (r > 180 && g > 140 && b < 160 && b < g) return 10; /* number amber */
  if (r > 150 && g > 60 && g < 120 && b < 50) return 14; /* light-theme number brown */
  if (r > 200 && g < 100) return 12;                  /* red */
  if (r > 200 && g > 100 && b < 100) return 14;       /* orange */
  for (i = 0; i < 16; i++) {
    int vr = (int)((VGA16[i] >> 16) & 255);
    int vg = (int)((VGA16[i] >> 8) & 255);
    int vb = (int)(VGA16[i] & 255);
    long dr = r - vr, dg = g - vg, db = b - vb;
    long d = dr * dr + dg * dg + db * db;
    {
      long dbri = bri - (vr + vg + vb);
      d += (dbri * dbri) / 4;
    }
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return (unsigned char)best;
}

static unsigned char make_attr(mote_u32 fg, mote_u32 bg) {
  unsigned char f = nearest_vga(fg) & 0x0f;
  /* VGA attribute bit7 is BLINK unless bright-bg mode sticks. Never put
   * indices 8..15 in the background nibble — that was the dark-theme flash. */
  unsigned char b = nearest_vga(bg) & 0x07;
  return (unsigned char)((b << 4) | f);
}

static int idx(Plat *p, int x, int y) { return y * p->cols + x; }

static mote_bool resize(Plat *p, int cols, int rows) {
  Cell *c, *pr;
  size_t n;
  if (cols < 40) cols = 40;
  if (rows < 10) rows = 10;
  if (cols > VGA_COLS) cols = VGA_COLS;
  if (rows > VGA_ROWS) rows = VGA_ROWS;
  n = (size_t)cols * (size_t)rows;
  c = (Cell *)calloc(n, sizeof(Cell));
  pr = (Cell *)calloc(n, sizeof(Cell));
  if (!c || !pr) {
    free(c);
    free(pr);
    return MOTE_FALSE;
  }
  free(p->cells);
  free(p->prev);
  p->cells = c;
  p->prev = pr;
  p->cols = cols;
  p->rows = rows;
  memset(pr, 0xff, n * sizeof(Cell)); /* force full redraw */
  return MOTE_TRUE;
}

/* MOTE_KEYTRACE=1: log raw getkey() codes and the keys they became. */
static void key_trace(int raw, const PlatEvent *e) {
  FILE *f;
  if (!getenv("MOTE_KEYTRACE") || !(f = fopen("KEYTRACE.LOG", "a"))) return;
  if (e) fprintf(f, "platkey=%d ctrl=%d shift=%d\n", (int)e->key, (int)e->ctrl, (int)e->shift);
  else fprintf(f, "raw=%d\n", raw);
  fclose(f);
}

static void key_flush(Plat *p, PlatKey k, mote_bool ctrl, mote_bool shift) {
  evq_key(&p->q, k, ctrl, shift);
  key_trace(0, &p->q.ev[p->q.n - 1]);
}

/* BIOS shift flags (0040:0017): bit0/1 = Shift, bit2 = Ctrl, bit3 = Alt. */
static unsigned bios_shifts(void) {
  return (unsigned)bioskey(_KEYBRD_SHIFTSTATUS);
}
static mote_bool shift_down(void) { return (bios_shifts() & 0x03u) != 0; }
static mote_bool ctrl_down(void) { return (bios_shifts() & 0x04u) != 0; }

static void emit(Plat *p, PlatKey k, mote_bool ctrl, mote_bool shift) {
  if (k != PK_NONE) key_flush(p, k, ctrl, shift);
}

/* Alt+letter arrives as 0x100 + keyboard scancode (row order, not ABC). */
static int alt_letter(int k) {
  static const char rows[] = "qwertyuiop" "\0\0\0\0" "asdfghjkl" "\0\0\0\0\0" "zxcvbnm";
  int sc = k - K_Alt_Q;
  return sc >= 0 && sc < (int)sizeof rows - 1 ? rows[sc] : 0;
}

/* F1-F10 plain / Shift / Ctrl / Alt, each a run of ten codes; 0 if not one. */
static int fn_number(int k, mote_bool *ctrl, mote_bool *shift) {
  *ctrl = *shift = MOTE_FALSE;
  if (k >= K_F1 && k < K_F1 + 10) return k - K_F1 + 1;
  if (k >= K_Shift_F1 && k < K_Shift_F1 + 10) {
    *shift = MOTE_TRUE;
    return k - K_Shift_F1 + 1;
  }
  if (k >= K_Control_F1 && k < K_Control_F1 + 10) {
    *ctrl = MOTE_TRUE;
    return k - K_Control_F1 + 1;
  }
  if (k >= K_Alt_F1 && k < K_Alt_F1 + 10) {
    *ctrl = MOTE_TRUE; /* Alt+F4 closes the file like Ctrl+F4 */
    return k - K_Alt_F1 + 1;
  }
  return 0;
}

static PlatKey nav_key(int k, mote_bool *ctrl, mote_bool *shift) {
  *ctrl = *shift = MOTE_FALSE;
  switch (k) {
  case K_Left: return PK_LEFT;
  case K_Right: return PK_RIGHT;
  case K_Up: return PK_UP;
  case K_Down: return PK_DOWN;
  case K_Home: return PK_HOME;
  case K_End: return PK_END;
  case K_PageUp: return PK_PGUP;
  case K_PageDown: return PK_PGDN;
  case K_Delete: return PK_DELETE;
  case K_BackTab: *shift = MOTE_TRUE; return PK_TAB;
  case K_Control_Left: *ctrl = MOTE_TRUE; return PK_LEFT;
  case K_Control_Right: *ctrl = MOTE_TRUE; return PK_RIGHT;
  case K_Control_Home: *ctrl = MOTE_TRUE; return PK_HOME;
  case K_Control_End: *ctrl = MOTE_TRUE; return PK_END;
  case K_Alt_Equals: return PK_ZOOMIN;
  default: return PK_NONE;
  }
}

static void ingest_key(Plat *p, int k) {
  mote_bool ctrl, shift;
  PlatKey pk;
  int n;
  key_trace(k, NULL);
  /* getkey(): 8 = BS, 9 = Tab, 13 = Enter, 27 = Esc share codes with
     Ctrl+H/I/M/[ and must be checked before the Ctrl+letter range. */
  if (k == 8 || k == 127 || k == K_BackSpace || k == K_Control_Backspace) {
    key_flush(p, PK_BACKSPACE, MOTE_FALSE, MOTE_FALSE);
  } else if (k == 9) { /* Tab and Ctrl+Tab share code 9: ask the BIOS */
    ctrl = ctrl_down();
    shift = shift_down();
    key_flush(p, ctrl ? key_ctrl('\t', shift) : PK_TAB, ctrl, shift);
  } else if (k == 13) {
    key_flush(p, PK_ENTER, MOTE_FALSE, MOTE_FALSE);
  } else if (k == 27 || k == K_Escape) {
    key_flush(p, PK_ESCAPE, MOTE_FALSE, MOTE_FALSE);
  } else if (k == K_Control_Caret) { /* Ctrl+= has no code; Ctrl+6 zooms in */
    key_flush(p, PK_ZOOMIN, MOTE_TRUE, MOTE_FALSE);
  } else if (k >= 1 && k < 32) { /* Ctrl+A..Z, Ctrl+\ ] _ */
    shift = shift_down();
    emit(p, key_ctrl(k | 0x40, shift), MOTE_TRUE, shift);
  } else if (k >= 32 && k < 127) {
    char ch = (char)k;
    evq_text(&p->q, &ch, 1);
  } else if (alt_letter(k)) {
    emit(p, key_alt(alt_letter(k)), MOTE_FALSE, MOTE_FALSE);
  } else if ((n = fn_number(k, &ctrl, &shift)) != 0) {
    emit(p, key_fn(n, ctrl, shift), ctrl, shift);
  } else if ((pk = nav_key(k, &ctrl, &shift)) != PK_NONE) {
    key_flush(p, pk, ctrl, shift);
  }
}

static void hide_hw_cursor(void) {
  __dpmi_regs r;
  memset(&r, 0, sizeof r);
  r.x.ax = 0x0100;
  r.x.cx = 0x2000; /* disable */
  __dpmi_int(0x10, &r);
}

static void set_text_mode(void) {
  __dpmi_regs r;
  memset(&r, 0, sizeof r);
  r.x.ax = 0x0003; /* 80x25 color text */
  __dpmi_int(0x10, &r);
  /* Attribute bit7 = bright background (not blink). Without this, any
   * bg index >= 8 makes the whole cell flash — dark theme hit this. */
  memset(&r, 0, sizeof r);
  r.x.ax = 0x1003;
  r.x.bx = 0; /* BH=0 BL=0: bright background, disable blink */
  __dpmi_int(0x10, &r);
  hide_hw_cursor();
}

/* Upload CP866+ASCII Terminus glyphs so Cyrillic text is readable. */
static void vga_load_cp866_font(void) {
  int sel = 0;
  int seg;
  __dpmi_regs r;
  seg = __dpmi_allocate_dos_memory((256 * 16 + 15) / 16, &sel);
  if (seg == -1) return;
  dosmemput(DOS_CP866_FONT, 256 * 16, (unsigned long)seg << 4);
  memset(&r, 0, sizeof r);
  r.x.ax = 0x1110; /* load user font */
  r.h.bh = 16;
  r.h.bl = 0;
  r.x.cx = 256;
  r.x.dx = 0;
  r.x.es = (unsigned short)seg;
  r.x.bp = 0;
  __dpmi_int(0x10, &r);
  __dpmi_free_dos_memory(sel);
  /* Font load can restore blink; re-enable bright backgrounds. */
  memset(&r, 0, sizeof r);
  r.x.ax = 0x1003;
  r.x.bx = 0; /* BH=0 BL=0: bright background, disable blink */
  __dpmi_int(0x10, &r);
  hide_hw_cursor();
}

static void poke_cell(int x, int y, unsigned char ch, unsigned char attr) {
  unsigned long addr = VGA_TEXT + (unsigned long)((y * VGA_COLS + x) * 2);
  _farpokeb(_dos_ds, addr, ch);
  _farpokeb(_dos_ds, addr + 1, attr);
}

Plat *plat_create(const char *title, int w, int h) {
  Plat *p;
  int cols = VGA_COLS, rows = VGA_ROWS;
  (void)title;
  p = (Plat *)calloc(1, sizeof *p);
  if (!p) return NULL;
  p->font_px = MOTE_FONT_PX;
  if (w >= 40 && w <= VGA_COLS) cols = w;
  if (h >= 10 && h <= VGA_ROWS) rows = h;
  set_text_mode();
  vga_load_cp866_font();
  vga_load_palette();
  if (!resize(p, cols, rows)) {
    free(p);
    return NULL;
  }
  return p;
}

void plat_destroy(Plat *p) {
  if (!p) return;
  set_text_mode();
  free(p->clip);
  free(p->cells);
  free(p->prev);
  free(p);
}

void plat_wait(Plat *p) {
  evq_flush_text(&p->q);
  if (p->q.n > 0) return;
  while (!kbhit()) {
    /* yield a bit */
    __dpmi_yield();
  }
}

mote_bool plat_poll(Plat *p, PlatEvent *ev) {
  memset(ev, 0, sizeof *ev);
  if (evq_pop(&p->q, ev)) return MOTE_TRUE;
  if (!kbhit()) {
    evq_flush_text(&p->q);
    return evq_pop(&p->q, ev);
  }
  ingest_key(p, getkey());
  evq_flush_text(&p->q);
  return evq_pop(&p->q, ev);
}

void plat_get_size(Plat *p, int *w, int *h) {
  if (w) *w = p->cols;
  if (h) *h = p->rows;
}
int plat_font_w(Plat *p) {
  (void)p;
  return 1;
}
int plat_font_h(Plat *p) {
  (void)p;
  return 1;
}
void plat_set_font_px(Plat *p, int px) {
  if (px < MOTE_FONT_MIN) px = MOTE_FONT_MIN;
  if (px > MOTE_FONT_MAX) px = MOTE_FONT_MAX;
  p->font_px = px;
}
int plat_font_px(Plat *p) { return p->font_px; }

void plat_begin_frame(Plat *p) { (void)p; }

void plat_clear(Plat *p, mote_u32 rgb) {
  int i, n = p->cols * p->rows;
  unsigned char attr = make_attr(0xD4D4D4ul, rgb);
  for (i = 0; i < n; i++) {
    p->cells[i].ch = ' ';
    p->cells[i].attr = attr;
    p->cells[i].fg = 0xD4D4D4ul;
    p->cells[i].bg = rgb;
  }
}

void plat_fill_rect(Plat *p, int x, int y, int w, int h, mote_u32 rgb) {
  int xi, yi, x1 = x + w, y1 = y + h;
  if (w <= 0 || h <= 0) return;
  if (x < 0) x = 0;
  if (y < 0) y = 0;
  if (x1 > p->cols) x1 = p->cols;
  if (y1 > p->rows) y1 = p->rows;
  for (yi = y; yi < y1; yi++)
    for (xi = x; xi < x1; xi++) {
      Cell *c = &p->cells[idx(p, xi, yi)];
      c->ch = ' ';
      c->fg = 0xD4D4D4ul;
      c->bg = rgb;
      c->attr = make_attr(c->fg, rgb);
    }
}

void plat_draw_text(Plat *p, int x, int y, const char *s, int n, mote_u32 rgb) {
  int i = 0, cx = x;
  if (!s || n <= 0 || y < 0 || y >= p->rows) return;
  while (i < n && cx < p->cols) {
    mote_u32 cp;
    int len = utf8_decode(s + i, (size_t)(n - i), &cp);
    if (len <= 0) {
      i++;
      continue;
    }
    if (cx >= 0) {
      Cell *c = &p->cells[idx(p, cx, y)];
      c->ch = cp_to_dos(cp);
      c->fg = rgb;
      c->attr = make_attr(rgb, c->bg);
    }
    cx++;
    i += len;
  }
}

void plat_end_frame(Plat *p) {
  int y, x;
  for (y = 0; y < p->rows; y++) {
    for (x = 0; x < p->cols; x++) {
      Cell *c = &p->cells[idx(p, x, y)];
      Cell *pr = &p->prev[idx(p, x, y)];
      unsigned char ch = c->ch ? c->ch : ' ';
      unsigned char attr = c->attr;
      if (p->caret_on && x == p->caret_x && y == p->caret_y) {
        /* Reverse video; keep both nibbles in 0..7 so bit7 never blinks. */
        unsigned char f = attr & 0x07;
        unsigned char b = (attr >> 4) & 0x07;
        attr = (unsigned char)((f << 4) | b);
        if (f == b) attr ^= 0x77;
      }
      if (pr->ch != ch || pr->attr != attr) {
        poke_cell(x, y, ch, attr);
        pr->ch = ch;
        pr->attr = attr;
      }
    }
  }
}

void plat_set_title(Plat *p, const char *title) {
  (void)p;
  (void)title; /* DOS text mode: no window title */
}

mote_bool plat_set_caret(Plat *p, int x, int y, int h, mote_bool on) {
  (void)h;
  p->caret_x = x;
  p->caret_y = y;
  p->caret_on = on;
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
