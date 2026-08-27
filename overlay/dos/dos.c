/* mote overlay/dos — VGA text mode (DJGPP / FreeDOS) */
#include "platform.h"
#include "utf8.h"

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

/* VGA text cell: char + attribute */
typedef struct {
  unsigned char ch;
  unsigned char attr;
  mote_u32 fg, bg; /* last RGB asked (for attr remap) */
} Cell;

struct Plat {
  int cols, rows, font_px, caret_x, caret_y, q_n, text_n;
  mote_bool caret_on;
  Cell *cells;
  Cell *prev; /* for dirty redraw */
  char *clip;
  size_t clip_n;
  unsigned short vseg; /* B800 */
  char text_acc[32];
  PlatEvent q[128];
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
  if (cols > 132) cols = 132;
  if (rows > 60) rows = 60;
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

static void qpush(Plat *p, PlatEvent *e) {
  if (p->q_n < (int)(sizeof p->q / sizeof p->q[0])) p->q[p->q_n++] = *e;
}

static mote_bool qpop(Plat *p, PlatEvent *e) {
  if (p->q_n <= 0) return MOTE_FALSE;
  *e = p->q[0];
  p->q_n--;
  memmove(p->q, p->q + 1, (size_t)p->q_n * sizeof p->q[0]);
  return MOTE_TRUE;
}

static void key(Plat *p, PlatKey k, mote_bool ctrl, mote_bool shift) {
  PlatEvent e;
  memset(&e, 0, sizeof e);
  e.type = PE_KEY;
  e.key = k;
  e.ctrl = ctrl;
  e.shift = shift;
  qpush(p, &e);
}

static void text1(Plat *p, const char *s, int n) {
  PlatEvent e;
  memset(&e, 0, sizeof e);
  e.type = PE_TEXT;
  if (n > (int)sizeof e.text) n = (int)sizeof e.text;
  if (n <= 0) return;
  memcpy(e.text, s, (size_t)n);
  e.text_len = n;
  qpush(p, &e);
}

static void text_flush(Plat *p) {
  if (p->text_n <= 0) return;
  text1(p, p->text_acc, p->text_n);
  p->text_n = 0;
}

static void text_add(Plat *p, const char *s, int n) {
  int i;
  for (i = 0; i < n; i++) {
    if (p->text_n >= (int)sizeof p->text_acc) text_flush(p);
    p->text_acc[p->text_n++] = s[i];
  }
}

static void key_trace(int raw, const char *action) {
  FILE *f;
  if (!getenv("MOTE_KEYTRACE")) return;
  f = fopen("KEYTRACE.LOG", "a");
  if (!f) return;
  fprintf(f, "raw=%d action=%s\n", raw, action ? action : "?");
  fclose(f);
}

static void key_flush(Plat *p, PlatKey k, mote_bool ctrl, mote_bool shift) {
  text_flush(p);
  if (getenv("MOTE_KEYTRACE")) {
    FILE *f = fopen("KEYTRACE.LOG", "a");
    if (f) {
      fprintf(f, "platkey=%d ctrl=%d shift=%d\n", (int)k, (int)ctrl, (int)shift);
      fclose(f);
    }
  }
  key(p, k, ctrl, shift);
}

/* BIOS shift flags (0040:0017): bit0/1 = Shift, bit2 = Ctrl, bit3 = Alt. */
static unsigned bios_shifts(void) {
  return (unsigned)bioskey(_KEYBRD_SHIFTSTATUS);
}
static mote_bool shift_down(void) { return (bios_shifts() & 0x03u) != 0; }
static mote_bool ctrl_down(void) { return (bios_shifts() & 0x04u) != 0; }

static void ctrl_key(Plat *p, int c, mote_bool shift) {
  char lo = (char)c;
  PlatKey k = PK_NONE;
  if (lo >= 'A' && lo <= 'Z') lo = (char)(lo - 'A' + 'a');
  switch (lo) {
  case 's': k = shift ? PK_SAVEAS : PK_SAVE; break;
  case 'o': k = PK_OPEN; break;
  case 'q': k = PK_QUIT; break;
  case 'z': k = PK_UNDO; break;
  case 'y': k = PK_REDO; break;
  case 'f': k = PK_FIND; break;
  case 'g': k = PK_GOTO; break;
  case 'r': k = shift ? PK_READONLY : PK_REPLACE; break;
  case 'x': k = PK_CUT; break;
  case 'c': k = PK_COPY; break;
  case 'v': k = PK_PASTE; break;
  case 'a': k = PK_SELALL; break;
  case 't': k = PK_THEME; break;
  case 'w': k = shift ? PK_CLOSEDOC : PK_WRAP; break;
  case 'd': k = PK_DUPLINE; break;
  case 'n': k = PK_NEWDOC; break;
  case 'e': k = shift ? PK_EOL : PK_RECENT; break;
  case 'k':
    if (shift) k = PK_DELLINE;
    break;
  default: break;
  }
  if (k != PK_NONE) key_flush(p, k, MOTE_TRUE, shift);
}

static void alt_letter(Plat *p, int c) {
  /* Mirrors soft_keys.h Alt bindings (reliable when Ctrl+Shift is awkward). */
  PlatKey k = PK_NONE;
  if (c >= 'a' && c <= 'z') c = c - 'a' + 'A';
  switch (c) {
  case 'C': k = PK_FINDCASE; break;
  case 'W': k = PK_FINDWORD; break;
  case 'S': k = PK_SAVEAS; break;
  case 'R': k = PK_READONLY; break;
  case 'K': k = PK_DELLINE; break;
  case 'E': k = PK_EOL; break;
  case 'H': k = PK_HELP; break;
  case 'N': k = PK_NEXTDOC; break;
  case 'P': k = PK_PREVDOC; break;
  default: break;
  }
  if (k != PK_NONE) key_flush(p, k, MOTE_FALSE, MOTE_FALSE);
}

static void ingest_key(Plat *p, int k) {
  /* DJGPP getkey(): 8=BS, 9=Tab/Ctrl+I, 13=Enter, 27=Esc must beat Ctrl+A..Z
   * (ASCII 1..26), because BS is also Ctrl+H (=8). */
  if (k == 8 || k == 127 || k == K_BackSpace || k == K_Control_Backspace) {
    key_trace(k, "backspace");
    key_flush(p, PK_BACKSPACE, MOTE_FALSE, MOTE_FALSE);
    return;
  }
  if (k == 9) {
    /* Tab / Ctrl+I / Ctrl+Tab share code 9 — use BIOS ctrl/shift. */
    if (ctrl_down()) {
      key_flush(p, shift_down() ? PK_PREVDOC : PK_NEXTDOC, MOTE_TRUE,
                shift_down());
    } else {
      key_flush(p, PK_TAB, MOTE_FALSE, MOTE_FALSE);
    }
    return;
  }
  if (k == 13) {
    key_flush(p, PK_ENTER, MOTE_FALSE, MOTE_FALSE);
    return;
  }
  if (k == 27 || k == K_Escape) {
    key_flush(p, PK_ESCAPE, MOTE_FALSE, MOTE_FALSE);
    return;
  }
  /* Ctrl+] / Ctrl+_ (zoom-) — outside 1..26 letter range. */
  if (k == K_Control_RBracket) {
    key_flush(p, PK_BRACKET, MOTE_TRUE, MOTE_FALSE);
    return;
  }
  if (k == K_Control_Underscore) {
    key_flush(p, PK_ZOOMOUT, MOTE_TRUE, MOTE_FALSE);
    return;
  }
  if (k == K_Control_Caret) {
    key_flush(p, PK_ZOOMIN, MOTE_TRUE, MOTE_FALSE);
    return;
  }
  if (k >= 1 && k <= 26) {
    ctrl_key(p, 'a' + k - 1, shift_down());
    return;
  }
  if (k >= 32 && k < 127) {
    char ch = (char)k;
    text_add(p, &ch, 1);
    return;
  }

  /* Extended / special (DJGPP keys.h) */
  switch (k) {
  case K_Left: key_flush(p, PK_LEFT, MOTE_FALSE, MOTE_FALSE); break;
  case K_Right: key_flush(p, PK_RIGHT, MOTE_FALSE, MOTE_FALSE); break;
  case K_Up: key_flush(p, PK_UP, MOTE_FALSE, MOTE_FALSE); break;
  case K_Down: key_flush(p, PK_DOWN, MOTE_FALSE, MOTE_FALSE); break;
  case K_Home: key_flush(p, PK_HOME, MOTE_FALSE, MOTE_FALSE); break;
  case K_End: key_flush(p, PK_END, MOTE_FALSE, MOTE_FALSE); break;
  case K_PageUp: key_flush(p, PK_PGUP, MOTE_FALSE, MOTE_FALSE); break;
  case K_PageDown: key_flush(p, PK_PGDN, MOTE_FALSE, MOTE_FALSE); break;
  case K_Delete: key_flush(p, PK_DELETE, MOTE_FALSE, MOTE_FALSE); break;
  case K_BackTab: key_flush(p, PK_TAB, MOTE_FALSE, MOTE_TRUE); break;
  case K_F1: key_flush(p, PK_HELP, MOTE_FALSE, MOTE_FALSE); break;
  case K_F2: key_flush(p, PK_NEXTDOC, MOTE_FALSE, MOTE_FALSE); break;
  case K_Shift_F2: key_flush(p, PK_PREVDOC, MOTE_FALSE, MOTE_TRUE); break;
  case K_F3: key_flush(p, PK_FINDNEXT, MOTE_FALSE, MOTE_FALSE); break;
  case K_Shift_F3: key_flush(p, PK_FINDPREV, MOTE_FALSE, MOTE_TRUE); break;
  case K_F5: key_flush(p, PK_RELOAD, MOTE_FALSE, MOTE_FALSE); break;
  case K_F7: key_flush(p, PK_WS, MOTE_FALSE, MOTE_FALSE); break;
  case K_Control_F4:
  case K_Alt_F4: key_flush(p, PK_CLOSEDOC, MOTE_TRUE, MOTE_FALSE); break;
  case K_Control_Left: key_flush(p, PK_LEFT, MOTE_TRUE, MOTE_FALSE); break;
  case K_Control_Right: key_flush(p, PK_RIGHT, MOTE_TRUE, MOTE_FALSE); break;
  case K_Control_Home: key_flush(p, PK_HOME, MOTE_TRUE, MOTE_FALSE); break;
  case K_Control_End: key_flush(p, PK_END, MOTE_TRUE, MOTE_FALSE); break;
  case K_Alt_Equals: key_flush(p, PK_ZOOMIN, MOTE_FALSE, MOTE_FALSE); break;
  case K_Alt_H: alt_letter(p, 'H'); break;
  case K_Alt_C: alt_letter(p, 'C'); break;
  case K_Alt_W: alt_letter(p, 'W'); break;
  case K_Alt_S: alt_letter(p, 'S'); break;
  case K_Alt_R: alt_letter(p, 'R'); break;
  case K_Alt_K: alt_letter(p, 'K'); break;
  case K_Alt_E: alt_letter(p, 'E'); break;
  case K_Alt_N: alt_letter(p, 'N'); break;
  case K_Alt_P: alt_letter(p, 'P'); break;
  default:
    break;
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

static void poke_cell(Plat *p, int x, int y, unsigned char ch, unsigned char attr) {
  unsigned long addr = 0xB8000UL + (unsigned long)((y * p->cols + x) * 2);
  (void)p->vseg;
  _farpokeb(_dos_ds, addr, ch);
  _farpokeb(_dos_ds, addr + 1, attr);
}

Plat *plat_create(const char *title, int w, int h) {
  Plat *p;
  int cols = 80, rows = 25;
  (void)title;
  p = (Plat *)calloc(1, sizeof *p);
  if (!p) return NULL;
  p->font_px = 16;
  p->vseg = 0xB800;
  if (w >= 40 && w <= 132) cols = w;
  if (h >= 10 && h <= 60) rows = h;
  set_text_mode();
  vga_load_cp866_font();
  vga_load_palette();
  /* If user asked 50 rows, try INT10 mode / font — keep 25 for max compat */
  if (rows > 25) rows = 25;
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
  text_flush(p);
  if (p->q_n > 0) return;
  while (!kbhit()) {
    /* yield a bit */
    __dpmi_yield();
  }
}

mote_bool plat_poll(Plat *p, PlatEvent *ev) {
  memset(ev, 0, sizeof *ev);
  if (qpop(p, ev)) return MOTE_TRUE;
  if (!kbhit()) {
    text_flush(p);
    return qpop(p, ev);
  }
  ingest_key(p, getkey());
  text_flush(p);
  return qpop(p, ev);
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
  if (px < 8) px = 8;
  if (px > 48) px = 48;
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
  }      if (pr->ch != ch || pr->attr != attr) {
        poke_cell(p, x, y, ch, attr);
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
