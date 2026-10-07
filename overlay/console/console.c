#include "platform.h"
#include "common.h"
#include "keymap.h"
#include "utf8.h"
#include "../evq.h"

#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

#ifdef __linux__
#if defined(MOTE_MUSL)
/* musl has no linux/kd.h; these ioctl numbers never change. */
#define KDGKBMODE 0x4B44
#define KDSKBMODE 0x4B45
#define K_UNICODE 0x03
#else
#include <linux/kd.h>
#endif
#endif

#define MAX_COLS 512
#define MAX_ROWS 256

typedef struct {
  mote_u32 cp, fg, bg;
} Cell;

struct Plat {
  int cols, rows, font_px, caret_x, caret_y, in_n;
  mote_bool caret_on, raw, paste, utf8, hw_caret;
  int color_mode; /* 0=16 1=256 2=truecolor */
  mote_bool geom_locked;
  Cell *cells;
  Cell *prev; /* what was painted last, to skip unchanged cells */
  char *clip;
  size_t clip_n;
  struct termios saved;
  char inbuf[64];
  unsigned char utf8_hold[4];
  int utf8_hold_n;
  int kbmode_saved;
  mote_bool kbmode_set;
  EvQueue q;
};

static volatile sig_atomic_t g_winch;
static void on_winch(int s) {
  (void)s;
  g_winch = 1;
}

static int idx(Plat *p, int x, int y) { return y * p->cols + x; }

static void clamp_size(int *cols, int *rows) {
  if (*cols < 1) *cols = 1;
  if (*rows < 1) *rows = 1;
  if (*cols > MAX_COLS) *cols = MAX_COLS;
  if (*rows > MAX_ROWS) *rows = MAX_ROWS;
}

static mote_bool resize(Plat *p, int cols, int rows) {
  Cell *c, *prev;
  size_t n;
  clamp_size(&cols, &rows);
  n = (size_t)cols * (size_t)rows;
  c = (Cell *)calloc(n, sizeof(Cell));
  prev = (Cell *)calloc(n, sizeof(Cell));
  if (!c || !prev) {
    free(c);
    free(prev);
    return MOTE_FALSE;
  }
  free(p->cells);
  free(p->prev);
  p->cells = c;
  p->prev = prev;
  p->cols = cols;
  p->rows = rows;
  /* Repaint everything after a resize. */
  for (n = 0; n < (size_t)cols * (size_t)rows; n++) {
    p->prev[n].cp = (mote_u32)~0u;
    p->prev[n].fg = 0;
    p->prev[n].bg = 0;
  }
  return MOTE_TRUE;
}

static void tty_size(int *cols, int *rows) {
  struct winsize ws;
  *cols = 80;
  *rows = 24;
  if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0) {
    if (ws.ws_col) *cols = ws.ws_col;
    if (ws.ws_row) *rows = ws.ws_row;
  }
}

/* 0=16-color (Linux VT), 1=256, 2=truecolor */
static int detect_color_mode(void) {
  const char *ct = getenv("COLORTERM");
  const char *term = getenv("TERM");
  if (getenv("MOTE_TRUECOLOR")) return 2;
  if (getenv("MOTE_NO_TRUECOLOR")) {
    if (term && (!strcmp(term, "linux") || !strcmp(term, "console"))) return 0;
    return 1;
  }
  if (ct && (strstr(ct, "truecolor") || strstr(ct, "24bit"))) return 2;
  if (!term || !term[0]) return 0;
  if (!strcmp(term, "linux") || !strcmp(term, "console") || !strcmp(term, "dumb") ||
      !strcmp(term, "vt100") || !strcmp(term, "vt102") || !strcmp(term, "ansi"))
    return 0;
  if (strstr(term, "-direct") || strstr(term, "truecolor")) return 2;
  return 1;
}

/* UTF-8 by default; MOTE_NO_UTF8 switches to single bytes and meta-bit Alt. */
static int detect_utf8_console(void) {
  const char *e;
  FILE *f;
  int v;
  e = getenv("MOTE_NO_UTF8");
  if (e && e[0] && e[0] != '0') return 0;
  e = getenv("MOTE_UTF8");
  if (e && e[0] && e[0] != '0') return 1;
  e = getenv("LC_ALL");
  if (!e || !e[0]) e = getenv("LC_CTYPE");
  if (!e || !e[0]) e = getenv("LANG");
  if (e && (strstr(e, "UTF-8") || strstr(e, "utf8") || strstr(e, "UTF8")))
    return 1;
  f = fopen("/sys/module/vt/parameters/default_utf8", "r");
  if (f) {
    v = 1;
    if (fscanf(f, "%d", &v) == 1) {
      fclose(f);
      return v != 0;
    }
    fclose(f);
  }
  e = getenv("TERM");
  if (e && strcmp(e, "linux") && strcmp(e, "console") && strcmp(e, "dumb"))
    return 1;
  return 1;
}

static int rgb_to_256(mote_u32 rgb) {
  int r = (int)((rgb >> 16) & 255), g = (int)((rgb >> 8) & 255), b = (int)(rgb & 255);
  if (r == g && g == b) {
    if (r < 8) return 16;
    if (r > 248) return 231;
    return 232 + (r - 8) * 24 / 247;
  }
  return 16 + 36 * (r * 5 / 255) + 6 * (g * 5 / 255) + (b * 5 / 255);
}

static int rgb_to_16(mote_u32 rgb) {
  int r = (int)((rgb >> 16) & 255), g = (int)((rgb >> 8) & 255), b = (int)(rgb & 255);
  int bright = (r + g + b) >= 380;
  int idx = (r >= 128 ? 1 : 0) | (g >= 128 ? 2 : 0) | (b >= 128 ? 4 : 0);
  /* keep the status bar strong blue with white text */
  if (b > r + 40 && b > g + 40 && b >= 100) idx = 4;
  if (r > 200 && g > 200 && b > 200) idx = 7;
  return bright ? idx + 8 : idx;
}

static void sgr_fg(Plat *p, mote_u32 rgb) {
  if (p->color_mode >= 2)
    printf("\033[38;2;%u;%u;%um", (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
  else if (p->color_mode == 1)
    printf("\033[38;5;%dm", rgb_to_256(rgb));
  else {
    int c = rgb_to_16(rgb);
    if (c < 8) printf("\033[3%dm", c);
    else printf("\033[9%dm", c - 8);
  }
}

static void sgr_bg(Plat *p, mote_u32 rgb) {
  if (p->color_mode >= 2)
    printf("\033[48;2;%u;%u;%um", (rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255);
  else if (p->color_mode == 1)
    printf("\033[48;5;%dm", rgb_to_256(rgb));
  else {
    int c = rgb_to_16(rgb);
    if (c < 8) printf("\033[4%dm", c);
    else printf("\033[10%dm", c - 8);
  }
}

static void text_utf8_cp(Plat *p, mote_u32 cp) {
  char u[4];
  int n = utf8_encode(cp, u);
  if (n > 0) evq_text(&p->q, u, n);
}

static void emit(Plat *p, PlatKey k, mote_bool ctrl, mote_bool shift) {
  if (k != PK_NONE) evq_key(&p->q, k, ctrl, shift);
}

/* C0 byte typed with Ctrl (Tab/Enter/Backspace/Esc are handled before).
   Terminals send Ctrl+/ as 0x1f, which would otherwise read as Ctrl+_. */
static void ctrl_byte(Plat *p, unsigned char c) {
  emit(p, key_ctrl(c == 0x1f ? '/' : c | 0x40, MOTE_FALSE), MOTE_TRUE, MOTE_FALSE);
}

static void alt_char(Plat *p, char ch) {
  PlatKey k = key_alt(ch);
  if (k != PK_NONE) evq_key(&p->q, k, MOTE_FALSE, MOTE_FALSE);
  else if ((unsigned char)ch >= 32) evq_text(&p->q, &ch, 1);
}

/* Terminals rarely report Ctrl+F4, so plain F4 closes the file; desktop
   terminals often grab F1, so F10-F12 open help too. */
static void fkey(Plat *p, int n, mote_bool ctrl, mote_bool shift) {
  if (n >= 10) n = 1;
  emit(p, key_fn(n, ctrl || n == 4, shift), ctrl, shift);
}

/* A key reported as codepoint + modifiers (modifyOtherKeys, CSI u). */
static void modified_char(Plat *p, int ch, mote_bool shift, mote_bool alt, mote_bool ctrl) {
  if (ctrl) emit(p, key_ctrl(ch, shift), MOTE_TRUE, shift);
  else if (ch == '\r') evq_key(&p->q, PK_ENTER, MOTE_FALSE, shift);
  else if (ch == '\t') evq_key(&p->q, PK_TAB, MOTE_FALSE, shift);
  else if (ch == 0x7f || ch == 0x08) evq_key(&p->q, PK_BACKSPACE, MOTE_FALSE, shift);
  else if (ch == 0x1b) evq_key(&p->q, PK_ESCAPE, MOTE_FALSE, MOTE_FALSE);
  else if (alt && ch < 0x80) alt_char(p, (char)ch);
  else if (ch >= 32) text_utf8_cp(p, (mote_u32)ch);
}

/* CSI <code> [; <mod>] ~ */
static void tilde_key(Plat *p, const int *par, int np, mote_bool shift, mote_bool alt,
                      mote_bool ctrl) {
  int code = par[0];
  PlatKey k = PK_NONE;
  switch (code) {
  case 27: /* xterm modifyOtherKeys: CSI 27 ; mod ; char ~ */
    if (np >= 3) modified_char(p, par[2], shift, alt, ctrl);
    return;
  case 200: p->paste = MOTE_TRUE; return;
  case 201:
    p->paste = MOTE_FALSE;
    evq_flush_text(&p->q);
    return;
  case 1: case 7: k = PK_HOME; break;
  case 4: case 8: k = PK_END; break;
  case 3: k = PK_DELETE; break;
  case 5: k = PK_PGUP; break;
  case 6: k = PK_PGDN; break;
  default:
    if (code >= 11 && code <= 15) fkey(p, code - 10, ctrl, shift);
    else if (code >= 17 && code <= 21) fkey(p, code - 11, ctrl, shift);
    else if (code == 23 || code == 24) fkey(p, code - 12, ctrl, shift);
    return;
  }
  evq_key(&p->q, k, ctrl, shift);
}

/* Decode p->inbuf (starts with ESC). Returns 0 while the sequence is incomplete. */
static int finish_esc(Plat *p) {
  const char *b = p->inbuf;
  int n = p->in_n, par[8], np = 0, v = 0, mod, j;
  mote_bool shift, alt, ctrl;
  char fin;

  if (n < 2) return 0;
  if (b[1] == 'O') { /* SS3: F1-F4, Home/End */
    if (n < 3) return 0;
    p->in_n = 0;
    if (b[2] >= 'P' && b[2] <= 'S') fkey(p, b[2] - 'P' + 1, MOTE_FALSE, MOTE_FALSE);
    else if (b[2] == 'H') evq_key(&p->q, PK_HOME, MOTE_FALSE, MOTE_FALSE);
    else if (b[2] == 'F') evq_key(&p->q, PK_END, MOTE_FALSE, MOTE_FALSE);
    return 1;
  }
  if (b[1] != '[') { /* ESC + char = Alt+char */
    p->in_n = 0;
    alt_char(p, b[1]);
    return 1;
  }
  if (n < 3) return 0;
  if (b[2] == '[') { /* Linux VT F1-F5: ESC [ [ A..E */
    if (n < 4) return 0;
    p->in_n = 0;
    if (b[3] >= 'A' && b[3] <= 'E') fkey(p, b[3] - 'A' + 1, MOTE_FALSE, MOTE_FALSE);
    return 1;
  }
  fin = b[n - 1];
  if (fin < 0x40 || fin > 0x7e) return 0;
  p->in_n = 0;

  for (j = 2; j < n - 1; j++) {
    if (b[j] == ';') {
      if (np < 7) par[np++] = v;
      v = 0;
    } else if (b[j] >= '0' && b[j] <= '9') {
      v = v * 10 + (b[j] - '0');
    }
  }
  par[np++] = v;
  /* xterm modifier parameter: 1 + (1 Shift | 2 Alt | 4 Ctrl) */
  mod = np >= 2 && par[1] > 1 ? par[1] - 1 : 0;
  shift = (mod & 1) != 0;
  alt = (mod & 2) != 0;
  ctrl = (mod & 4) != 0;

  switch (fin) {
  case 'A': evq_key(&p->q, PK_UP, ctrl, shift); break;
  case 'B': evq_key(&p->q, PK_DOWN, ctrl, shift); break;
  case 'C': evq_key(&p->q, PK_RIGHT, ctrl, shift); break;
  case 'D': evq_key(&p->q, PK_LEFT, ctrl, shift); break;
  case 'H': evq_key(&p->q, PK_HOME, ctrl, shift); break;
  case 'F': evq_key(&p->q, PK_END, ctrl, shift); break;
  case 'P': case 'Q': case 'R': case 'S': fkey(p, fin - 'P' + 1, ctrl, shift); break;
  case 'Z': evq_key(&p->q, PK_TAB, MOTE_FALSE, MOTE_TRUE); break;
  case 'u': modified_char(p, par[0], shift, alt, ctrl); break;
  case '~': tilde_key(p, par, np, shift, alt, ctrl); break;
  default: break;
  }
  return 1;
}

static int utf8_seq_len(unsigned char c) {
  if (c < 0x80) return 1;
  if ((c & 0xE0) == 0xC0) return 2;
  if ((c & 0xF0) == 0xE0) return 3;
  if ((c & 0xF8) == 0xF0) return 4;
  return 0;
}

/* Assemble UTF-8 across read() boundaries; emit complete codepoints as text. */
static void feed_utf8_byte(Plat *p, unsigned char c) {
  int need;
  mote_u32 cp;
  int len;
  if (p->utf8_hold_n == 0) {
    need = utf8_seq_len(c);
    if (need <= 1) {
      if (c < 0x80)
        evq_text(&p->q, (const char *)&c, 1);
      else if (!p->utf8) {
        /* old consoles: a lone high byte (KOI8, CP866) passes through as Latin-1 */
        text_utf8_cp(p, (mote_u32)c);
      } else {
        /* stray continuation byte, skip it */
      }
      return;
    }
    p->utf8_hold[0] = c;
    p->utf8_hold_n = 1;
    return;
  }
  if ((c & 0xC0) != 0x80) {
    /* broken sequence: drop it and parse c again */
    p->utf8_hold_n = 0;
    feed_utf8_byte(p, c);
    return;
  }
  p->utf8_hold[p->utf8_hold_n++] = c;
  need = utf8_seq_len(p->utf8_hold[0]);
  if (p->utf8_hold_n < need) return;
  len = utf8_decode((const char *)p->utf8_hold, (size_t)p->utf8_hold_n, &cp);
  p->utf8_hold_n = 0;
  if (len > 0 && cp != 0xFFFDu)
    text_utf8_cp(p, cp);
}

static void ingest(Plat *p, const unsigned char *buf, int n) {
  int i;
  int burst = p->paste || n > 2;
  for (i = 0; i < n; i++) {
    unsigned char c = buf[i];

    if (p->in_n || c == 0x1b) {
      p->utf8_hold_n = 0;
      if (p->in_n < (int)sizeof p->inbuf - 1)
        p->inbuf[p->in_n++] = (char)c;
      else
        p->in_n = 0;
      if (c == 0x1b && p->in_n == 1) continue;
      (void)finish_esc(p);
      continue;
    }

    /* In UTF-8 mode high bytes are always UTF-8 (Cyrillic). They mean
       meta-bit Alt only with UTF-8 off, or D0 and D1 would read as Alt+P. */
    if (c >= 0x80) {
      if (p->utf8 || p->utf8_hold_n > 0) {
        feed_utf8_byte(p, c);
        continue;
      }
      {
        unsigned char ch = (unsigned char)(c & 0x7f);
        if (ch >= 1 && ch <= 26) ctrl_byte(p, ch);
        else if (ch >= 32 && ch != 0x7f) alt_char(p, (char)ch);
      }
      continue;
    }

    if (p->utf8_hold_n > 0) {
      /* ASCII interrupts incomplete UTF-8 */
      p->utf8_hold_n = 0;
    }

    if (c == 0x7f || c == 0x08) {
      evq_key(&p->q, PK_BACKSPACE, MOTE_FALSE, MOTE_FALSE);
      continue;
    }
    if (c == '\r' || c == '\n') {
      if (burst) {
        if (c == '\r' && i + 1 < n && buf[i + 1] == '\n') i++;
        evq_text(&p->q, "\n", 1);
      } else {
        evq_key(&p->q, PK_ENTER, MOTE_FALSE, MOTE_FALSE);
      }
      continue;
    }
    if (c == '\t') {
      if (burst)
        evq_text(&p->q, "\t", 1);
      else
        evq_key(&p->q, PK_TAB, MOTE_FALSE, MOTE_FALSE);
      continue;
    }
    if (c < 32) {
      ctrl_byte(p, c);
      continue;
    }

    feed_utf8_byte(p, c);
  }
}

static void flush_esc(Plat *p) {
  struct pollfd fd;
  unsigned char buf[32];
  ssize_t n;
  if (p->in_n != 1 || p->inbuf[0] != 0x1b) return;
  fd.fd = STDIN_FILENO;
  fd.events = POLLIN;
  /* wait for the rest of the escape sequence */
  if (poll(&fd, 1, 250) > 0) {
    n = read(STDIN_FILENO, buf, sizeof buf);
    if (n > 0) ingest(p, buf, (int)n);
    return;
  }
  p->in_n = 0;
  evq_key(&p->q, PK_ESCAPE, MOTE_FALSE, MOTE_FALSE);
}

static void check_winch(Plat *p) {
  int cols, rows;
  g_winch = 0;
  if (p->geom_locked) return;
  /* Re-read the size every time: some terminals never send SIGWINCH. */
  tty_size(&cols, &rows);
  clamp_size(&cols, &rows); /* else a huge terminal resizes on every poll */
  if ((cols != p->cols || rows != p->rows) && resize(p, cols, rows))
    evq_type(&p->q, PE_EXPOSE);
}

Plat *plat_create(const char *title, int w, int h) {
  Plat *p;
  int cols, rows;
  struct termios t;
  p = (Plat *)calloc(1, sizeof *p);
  if (!p) return NULL;
  p->font_px = MOTE_FONT_PX;
  p->color_mode = detect_color_mode();
  p->utf8 = detect_utf8_console() ? MOTE_TRUE : MOTE_FALSE;
  /* Hardware cursor: an inverted cell drifts on the Linux console when
     character widths disagree. */
  p->hw_caret = (p->color_mode == 0) ? MOTE_TRUE : MOTE_FALSE;
  tty_size(&cols, &rows);
  if (w >= 40 && w <= MAX_COLS && h >= 10 && h <= MAX_ROWS) {
    cols = w;
    rows = h;
    p->geom_locked = MOTE_TRUE;
  }
  if (!resize(p, cols, rows)) {
    free(p);
    return NULL;
  }
  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
    free(p->cells);
    free(p->prev);
    free(p);
    return NULL;
  }
  if (tcgetattr(STDIN_FILENO, &p->saved) != 0) {
    free(p->cells);
    free(p->prev);
    free(p);
    return NULL;
  }
  t = p->saved;
  cfmakeraw(&t);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 0;
  /* Turn off flow control so Ctrl+S and Ctrl+Q reach us. */
  t.c_iflag &= ~((tcflag_t)(IXON | IXOFF | IXANY));
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) != 0) {
    free(p->cells);
    free(p->prev);
    free(p);
    return NULL;
  }
  p->raw = MOTE_TRUE;
#ifdef __linux__
  /* Keyboard UTF-8 so Cyrillic arrives as multi-byte, not 8-bit meta. */
  if (p->utf8 && ioctl(STDIN_FILENO, KDGKBMODE, &p->kbmode_saved) == 0) {
    if (ioctl(STDIN_FILENO, KDSKBMODE, K_UNICODE) == 0)
      p->kbmode_set = MOTE_TRUE;
  }
#endif
  signal(SIGWINCH, on_winch);
  /* ESC % G puts the Linux console in UTF-8; without it Cyrillic takes two cells */
  if (p->utf8)
    fputs("\033%G", stdout);
  {
    const char *term = getenv("TERM");
    if (!term || (strcmp(term, "linux") && strcmp(term, "console")))
      fputs("\033[>4;1m", stdout); /* xterm modifyOtherKeys tells Ctrl+M from Enter */
  }
  fputs("\033[?1049h\033[?2004h\033[?25l\033[2J\033[H", stdout);
  if (title && title[0]) printf("\033]0;%s\007", title);
  fflush(stdout);
  return p;
}

void plat_destroy(Plat *p) {
  if (!p) return;
  if (p->raw) {
#ifdef __linux__
    if (p->kbmode_set)
      (void)ioctl(STDIN_FILENO, KDSKBMODE, p->kbmode_saved);
#endif
    /* Leave a clean TTY: Linux VT often ignores alt-screen, so always clear. */
    fputs("\033[>4;0m"
          "\033[?2004l"
          "\033[?25h"
          "\033[0m"
          "\033[?1049l"
          "\033%@"
          "\033[2J"
          "\033[H",
          stdout);
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &p->saved);
  }
  free(p->clip);
  free(p->cells);
  free(p->prev);
  free(p);
}

void plat_wait(Plat *p) {
  struct pollfd fd;
  flush_esc(p);
  check_winch(p);
  if (p->q.n > 0) return;
  fd.fd = STDIN_FILENO;
  fd.events = POLLIN;
  (void)poll(&fd, 1, -1);
}

mote_bool plat_poll(Plat *p, PlatEvent *ev) {
  unsigned char buf[4096];
  ssize_t n;
  memset(ev, 0, sizeof *ev);
  check_winch(p);
  flush_esc(p);
  if (evq_pop(&p->q, ev)) return MOTE_TRUE;
  /* leave room in queue so a big paste is not silently dropped */
  if (p->q.n > (int)(sizeof p->q / sizeof p->q.ev[0]) - 64) {
    evq_flush_text(&p->q);
    return evq_pop(&p->q, ev);
  }
  n = read(STDIN_FILENO, buf, sizeof buf);
  if (n > 0) ingest(p, buf, (int)n);
  flush_esc(p);
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

void plat_begin_frame(Plat *p) { check_winch(p); }

void plat_clear(Plat *p, mote_u32 rgb) {
  int i, n = p->cols * p->rows;
  for (i = 0; i < n; i++) {
    p->cells[i].cp = ' ';
    p->cells[i].fg = rgb; /* same color as the background: blank */
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
      c->cp = ' ';
      c->fg = rgb; /* invisible until plat_draw_text sets fg */
      c->bg = rgb;
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
      c->cp = cp < 32 ? (mote_u32)' ' : cp;
      c->fg = rgb;
    }
    cx++;
    i += len;
  }
}

void plat_end_frame(Plat *p) {
  int y, x;
  mote_u32 lfg = 0xffffffffu, lbg = 0xffffffffu;

  /* Dump the cell grid to a file, for screenshots. */
  {
    const char *path = getenv("MOTE_DUMP_CELLS");
    static int dumped;
    if (path && path[0] && p->cells && !dumped) {
      FILE *f = fopen(path, "wb");
      int i, n = p->cols * p->rows;
      dumped = 1;
      if (f) {
        fprintf(f, "MOTECELL %d %d\n", p->cols, p->rows);
        for (i = 0; i < n; i++) {
          Cell *c = &p->cells[i];
          unsigned char b[10];
          mote_u32 cp = c->cp ? c->cp : (mote_u32)' ';
          b[0] = (unsigned char)(cp & 255);
          b[1] = (unsigned char)((cp >> 8) & 255);
          b[2] = (unsigned char)((cp >> 16) & 255);
          b[3] = (unsigned char)((cp >> 24) & 255);
          b[4] = (unsigned char)((c->fg >> 16) & 255);
          b[5] = (unsigned char)((c->fg >> 8) & 255);
          b[6] = (unsigned char)(c->fg & 255);
          b[7] = (unsigned char)((c->bg >> 16) & 255);
          b[8] = (unsigned char)((c->bg >> 8) & 255);
          b[9] = (unsigned char)(c->bg & 255);
          fwrite(b, 1, 10, f);
        }
        fclose(f);
      }
      if (getenv("MOTE_SHOT_ONCE")) _exit(0);
    }
  }

  /* Rewrite only the rows that changed; less flicker on the console. */
  fputs("\033[?25l", stdout);
  for (y = 0; y < p->rows; y++) {
    int dirty = 0;
    for (x = 0; x < p->cols; x++) {
      Cell cur = p->cells[idx(p, x, y)];
      Cell old = p->prev[idx(p, x, y)];
      /* Invert the caret cell only without the hardware cursor. */
      if (!p->hw_caret && p->caret_on && x == p->caret_x && y == p->caret_y) {
        mote_u32 t = cur.fg;
        cur.fg = cur.bg;
        cur.bg = t;
      }
      if (cur.cp != old.cp || cur.fg != old.fg || cur.bg != old.bg) {
        dirty = 1;
        break;
      }
    }
    if (!dirty) continue;
    printf("\033[%d;1H", y + 1);
    lfg = 0xffffffffu;
    lbg = 0xffffffffu;
    for (x = 0; x < p->cols; x++) {
      Cell *c = &p->cells[idx(p, x, y)];
      mote_u32 fg = c->fg, bg = c->bg, cp = c->cp ? c->cp : (mote_u32)' ';
      char u[4];
      int un;
      if (!p->hw_caret && p->caret_on && x == p->caret_x && y == p->caret_y) {
        mote_u32 t = fg;
        fg = bg;
        bg = t;
      }
      if (fg != lfg) {
        sgr_fg(p, fg);
        lfg = fg;
      }
      if (bg != lbg) {
        sgr_bg(p, bg);
        lbg = bg;
      }
      un = utf8_encode(cp, u);
      if (un > 0) fwrite(u, 1, (size_t)un, stdout);
      else fputc('?', stdout);
      {
        Cell *o = &p->prev[idx(p, x, y)];
        o->cp = cp;
        o->fg = fg;
        o->bg = bg;
      }
    }
    fputs("\033[K", stdout);
  }
  if (p->hw_caret && p->caret_on && p->caret_x >= 0 && p->caret_y >= 0 &&
      p->caret_x < p->cols && p->caret_y < p->rows)
    printf("\033[%d;%dH\033[?25h", p->caret_y + 1, p->caret_x + 1);
  fflush(stdout);
}

void plat_set_title(Plat *p, const char *title) {
  (void)p;
  if (title) printf("\033]0;%s\007", title);
  fflush(stdout);
}

mote_bool plat_set_caret(Plat *p, int x, int y, int h, mote_bool on) {
  (void)h;
  p->caret_x = x;
  p->caret_y = y;
  p->caret_on = on;
  return MOTE_TRUE;
}

static char *clip_from_cmd(const char *cmd, size_t *out_len) {
  FILE *f;
  char *d = NULL, *nd;
  size_t n = 0, capa = 0;
  char buf[1024];
  size_t got;
  f = popen(cmd, "r");
  if (!f) return NULL;
  while ((got = fread(buf, 1, sizeof buf, f)) > 0) {
    if (n + got + 1 > capa) {
      size_t nc = capa ? capa * 2 : 4096;
      while (nc < n + got + 1) nc *= 2;
      if (nc > 2 * 1024 * 1024) break;
      nd = (char *)realloc(d, nc);
      if (!nd) {
        free(d);
        d = NULL;
        n = 0;
        break;
      }
      d = nd;
      capa = nc;
    }
    memcpy(d + n, buf, got);
    n += got;
  }
  pclose(f);
  if (!d || !n) {
    free(d);
    if (out_len) *out_len = 0;
    return NULL;
  }
  d[n] = 0;
  if (out_len) *out_len = n;
  return d;
}

char *plat_clipboard_get(Plat *p, size_t *out_len) {
  char *d;
  if (p->clip && p->clip_n) {
    d = (char *)malloc(p->clip_n + 1);
    if (!d) return NULL;
    memcpy(d, p->clip, p->clip_n);
    d[p->clip_n] = 0;
    if (out_len) *out_len = p->clip_n;
    return d;
  }
  /* system clipboard when internal empty (Ctrl+V) */
  d = clip_from_cmd("wl-paste -n 2>/dev/null", out_len);
  if (d) return d;
  return clip_from_cmd("xclip -selection clipboard -o 2>/dev/null", out_len);
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

#ifdef MOTE_TEST_CONSOLE_ESC
void console_test_feed(Plat *p, const unsigned char *buf, int n) {
  p->q.n = 0;
  ingest(p, buf, n);
  flush_esc(p);
  evq_flush_text(&p->q);
}

PlatKey console_test_last_key(const Plat *p) {
  int i;
  for (i = p->q.n - 1; i >= 0; i--)
    if (p->q.ev[i].type == PE_KEY) return p->q.ev[i].key;
  return PK_NONE;
}
#endif
