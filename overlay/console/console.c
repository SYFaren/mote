/* mote overlay/console — compact ANSI truecolor TTY (C99) */
#include "platform.h"
#include "utf8.h"

#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <unistd.h>

typedef struct {
  mote_u32 cp, fg, bg;
} Cell;

struct Plat {
  int cols, rows, font_px, caret_x, caret_y, in_n, q_n, text_n;
  mote_bool caret_on, raw, paste;
  Cell *cells;
  char *clip;
  size_t clip_n;
  struct termios saved;
  char inbuf[64];
  char text_acc[32];
  PlatEvent q[256];
};

static volatile sig_atomic_t g_winch;
static void on_winch(int s) {
  (void)s;
  g_winch = 1;
}

static int idx(Plat *p, int x, int y) { return y * p->cols + x; }

static mote_bool resize(Plat *p, int cols, int rows) {
  Cell *c;
  size_t n;
  if (cols < 1) cols = 1;
  if (rows < 1) rows = 1;
  if (cols > 512) cols = 512;
  if (rows > 256) rows = 256;
  n = (size_t)cols * (size_t)rows;
  c = (Cell *)calloc(n, sizeof(Cell));
  if (!c) return MOTE_FALSE;
  free(p->cells);
  p->cells = c;
  p->cols = cols;
  p->rows = rows;
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

static void key_flush(Plat *p, PlatKey k, mote_bool ctrl, mote_bool shift) {
  text_flush(p);
  key(p, k, ctrl, shift);
}

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
  case '=':
  case '+': k = PK_ZOOMIN; break;
  case '-':
  case '_': k = PK_ZOOMOUT; break;
  case '0': k = PK_ZOOMRESET; break;
  case ']': k = PK_BRACKET; break;
  default: break;
  }
  if (k != PK_NONE) key_flush(p, k, MOTE_TRUE, shift);
}

static int finish_esc(Plat *p) {
  char *b = p->inbuf;
  int n = p->in_n;
  PlatKey k = PK_NONE;
  mote_bool shift = MOTE_FALSE, ctrl = MOTE_FALSE;
  int code, j;

  if (n < 2) return 0;

  if (b[1] == 'O') { /* SS3: \033OP = F1 — may arrive as ESC O / ESC O P */
    if (n < 3) return 0;
    if (b[2] == 'P') k = PK_F1;
    else if (b[2] == 'H') k = PK_HOME;
    else if (b[2] == 'F') k = PK_END;
    p->in_n = 0;
    if (k != PK_NONE) key_flush(p, k, MOTE_FALSE, MOTE_FALSE);
    return 1;
  }

  if (b[1] != '[') {
    /* ESC + char → Alt+char (TTY Alt bindings; see soft_keys.h). */
    p->in_n = 0;
    if (n == 2) {
      char ch = b[1];
      char up = ch;
      PlatKey ak = PK_NONE;
      if (up >= 'a' && up <= 'z') up = (char)(up - 'a' + 'A');
      if (up == 'H') ak = PK_HELP;
      else if (up == 'C') ak = PK_FINDCASE;
      else if (up == 'W') ak = PK_FINDWORD;
      else if (up == 'S') ak = PK_SAVEAS;
      else if (up == 'R') ak = PK_READONLY;
      else if (up == 'K') ak = PK_DELLINE;
      else if (up == 'E') ak = PK_EOL;
      else if (up == 'N') ak = PK_NEXTDOC;
      else if (up == 'P') ak = PK_PREVDOC;
      if (ak != PK_NONE) key_flush(p, ak, MOTE_FALSE, MOTE_FALSE);
      else text_add(p, &ch, 1);
    }
    return 1;
  }

  /* CSI: ESC [ … final — need at least ESC [ X */
  if (n < 3) return 0;
  /* final byte is the last; must not still be collecting params */
  if (b[n - 1] < 0x40 || b[n - 1] > 0x7e) return 0;

  /* modifiers \033[1;N? */
  for (j = 2; j < n - 1; j++) {
    if (b[j] == ';' && j + 1 < n - 1) {
      int mod = b[j + 1] - '0';
      if (mod >= 2) {
        if (mod == 2 || mod == 6 || mod == 4) shift = MOTE_TRUE;
        if (mod == 5 || mod == 6 || mod == 4) ctrl = MOTE_TRUE;
      }
    }
  }

  switch (b[n - 1]) {
  case 'A': k = PK_UP; break;
  case 'B': k = PK_DOWN; break;
  case 'C': k = PK_RIGHT; break;
  case 'D': k = PK_LEFT; break;
  case 'H': k = PK_HOME; break;
  case 'F': k = PK_END; break;
  case 'Z': k = PK_TAB; shift = MOTE_TRUE; break; /* backtab */
  case '~':
    code = 0;
    for (j = 2; j < n - 1 && b[j] >= '0' && b[j] <= '9'; j++)
      code = code * 10 + (b[j] - '0');
    /* xterm: CSI 27 ; mod ; key ~  (Ctrl/Shift+Tab) */
    if (code == 27) {
      int parts[3] = {0, 0, 0}, pi = 0, v = 0;
      for (j = 2; j < n - 1; j++) {
        if (b[j] == ';') {
          if (pi < 3) parts[pi++] = v;
          v = 0;
        } else if (b[j] >= '0' && b[j] <= '9')
          v = v * 10 + (b[j] - '0');
      }
      if (pi < 3) parts[pi++] = v;
      if (parts[0] == 27 && parts[2] == 9) {
        int m = parts[1];
        mote_bool sh = (m == 2 || m == 4 || m == 6 || m == 8);
        mote_bool ct = (m == 5 || m == 6 || m == 7 || m == 8);
        if (ct)
          key_flush(p, sh ? PK_PREVDOC : PK_NEXTDOC, MOTE_TRUE, sh);
        else
          key_flush(p, PK_TAB, MOTE_FALSE, sh);
        p->in_n = 0;
        return 1;
      }
    }
    if (code == 200) { /* bracketed paste start */
      p->paste = MOTE_TRUE;
      p->in_n = 0;
      return 1;
    }
    if (code == 201) { /* bracketed paste end */
      p->paste = MOTE_FALSE;
      text_flush(p);
      p->in_n = 0;
      return 1;
    }
    if (code == 1 || code == 7) k = PK_HOME;
    else if (code == 4 || code == 8) k = PK_END;
    else if (code == 3) k = PK_DELETE;
    else if (code == 5) k = PK_PGUP;
    else if (code == 6) k = PK_PGDN;
    else if (code == 11 || code == 12) k = PK_F1;
    else if (code == 13 || code == 14) k = shift ? PK_FINDPREV : PK_FINDNEXT;
    else if (code == 15) k = PK_RELOAD;
    else if (code == 17) k = PK_F1; /* some terms */
    else if (code == 18) k = PK_WS;
    break;
  default:
    break;
  }
  p->in_n = 0;
  if (k != PK_NONE) key_flush(p, k, ctrl, shift);
  return 1;
}

static void ingest(Plat *p, const unsigned char *buf, int n) {
  int i;
  /* >2 bytes in one read ⇒ paste/burst (not a lone key / UTF-8 scalar) */
  int burst = p->paste || n > 2;
  for (i = 0; i < n; i++) {
    unsigned char c = buf[i];

    if (p->in_n || c == 0x1b) {
      if (p->in_n < (int)sizeof p->inbuf - 1)
        p->inbuf[p->in_n++] = (char)c;
      else
        p->in_n = 0;
      if (c == 0x1b && p->in_n == 1) continue;
      (void)finish_esc(p);
      continue;
    }

    if (c == 0x7f || c == 0x08) {
      key_flush(p, PK_BACKSPACE, MOTE_FALSE, MOTE_FALSE);
      continue;
    }
    if (c == '\r' || c == '\n') {
      if (burst) {
        /* paste: raw newlines, no auto-indent / no double CR+LF */
        if (c == '\r' && i + 1 < n && buf[i + 1] == '\n') i++;
        text_add(p, "\n", 1);
      } else {
        key_flush(p, PK_ENTER, MOTE_FALSE, MOTE_FALSE);
      }
      continue;
    }
    if (c == '\t') {
      if (burst)
        text_add(p, "\t", 1);
      else
        key_flush(p, PK_TAB, MOTE_FALSE, MOTE_FALSE);
      continue;
    }
    if (c >= 1 && c <= 26) {
      ctrl_key(p, 'a' + (int)c - 1, MOTE_FALSE);
      continue;
    }
    if (c < 32) continue;

    /* UTF-8 — batch into PE_TEXT so paste is one insert (no autoclose) */
    {
      char tmp[4];
      int have = 1;
      mote_u32 cp;
      int len;
      tmp[0] = (char)c;
      while (have < 4 && i + 1 < n && (buf[i + 1] & 0xc0) == 0x80)
        tmp[have++] = (char)buf[++i];
      len = utf8_decode(tmp, (size_t)have, &cp);
      if (len > 0) text_add(p, tmp, len);
    }
  }
}

static void flush_esc(Plat *p) {
  struct pollfd fd;
  unsigned char buf[32];
  ssize_t n;
  if (p->in_n != 1 || p->inbuf[0] != 0x1b) return;
  fd.fd = STDIN_FILENO;
  fd.events = POLLIN;
  /* wait briefly for CSI/SS3 tail (F1 = ESC O P) */
  if (poll(&fd, 1, 120) > 0) {
    n = read(STDIN_FILENO, buf, sizeof buf);
    if (n > 0) ingest(p, buf, (int)n);
    return;
  }
  p->in_n = 0;
  key_flush(p, PK_ESCAPE, MOTE_FALSE, MOTE_FALSE);
}

static void check_winch(Plat *p) {
  int cols, rows;
  PlatEvent e;
  if (!g_winch) return;
  g_winch = 0;
  tty_size(&cols, &rows);
  if (cols == p->cols && rows == p->rows) return;
  if (!resize(p, cols, rows)) return;
  memset(&e, 0, sizeof e);
  e.type = PE_EXPOSE;
  qpush(p, &e);
}

Plat *plat_create(const char *title, int w, int h) {
  Plat *p;
  int cols, rows;
  struct termios t;
  p = (Plat *)calloc(1, sizeof *p);
  if (!p) return NULL;
  p->font_px = 15;
  tty_size(&cols, &rows);
  if (w >= 40 && w <= 512 && h >= 10 && h <= 256) {
    cols = w;
    rows = h;
  }
  if (!resize(p, cols, rows)) {
    free(p);
    return NULL;
  }
  if (!isatty(STDIN_FILENO) || !isatty(STDOUT_FILENO)) {
    free(p->cells);
    free(p);
    return NULL;
  }
  if (tcgetattr(STDIN_FILENO, &p->saved) != 0) {
    free(p->cells);
    free(p);
    return NULL;
  }
  t = p->saved;
  cfmakeraw(&t);
  t.c_cc[VMIN] = 0;
  t.c_cc[VTIME] = 0;
  if (tcsetattr(STDIN_FILENO, TCSAFLUSH, &t) != 0) {
    free(p->cells);
    free(p);
    return NULL;
  }
  p->raw = MOTE_TRUE;
  signal(SIGWINCH, on_winch);
  fputs("\033[?1049h\033[?2004h\033[?25l\033[2J\033[H", stdout);
  if (title && title[0]) printf("\033]0;%s\007", title);
  fflush(stdout);
  return p;
}

void plat_destroy(Plat *p) {
  if (!p) return;
  if (p->raw) {
    fputs("\033[?2004l\033[?25h\033[?1049l", stdout);
    fflush(stdout);
    tcsetattr(STDIN_FILENO, TCSAFLUSH, &p->saved);
  }
  free(p->clip);
  free(p->cells);
  free(p);
}

void plat_wait(Plat *p) {
  struct pollfd fd;
  flush_esc(p);
  check_winch(p);
  if (p->q_n > 0) return;
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
  if (qpop(p, ev)) return MOTE_TRUE;
  /* leave room in queue so a big paste is not silently dropped */
  if (p->q_n > (int)(sizeof p->q / sizeof p->q[0]) - 64) {
    text_flush(p);
    return qpop(p, ev);
  }
  n = read(STDIN_FILENO, buf, sizeof buf);
  if (n > 0) ingest(p, buf, (int)n);
  flush_esc(p);
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
  for (i = 0; i < n; i++) {
    p->cells[i].cp = ' ';
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
    for (xi = x; xi < x1; xi++) p->cells[idx(p, xi, yi)].bg = rgb;
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
  /* Hide cursor; paint full grid with truecolor (status = last row). */
  fputs("\033[?25l\033[H", stdout);
  for (y = 0; y < p->rows; y++) {
    if (y > 0) printf("\033[%d;1H", y + 1);
    for (x = 0; x < p->cols; x++) {
      Cell *c = &p->cells[idx(p, x, y)];
      mote_u32 fg = c->fg, bg = c->bg, cp = c->cp ? c->cp : (mote_u32)' ';
      char u[4];
      int un;
      if (p->caret_on && x == p->caret_x && y == p->caret_y) {
        mote_u32 t = fg;
        fg = bg;
        bg = t;
      }
      if (fg != lfg) {
        printf("\033[38;2;%u;%u;%um", (fg >> 16) & 255, (fg >> 8) & 255,
               fg & 255);
        lfg = fg;
      }
      if (bg != lbg) {
        printf("\033[48;2;%u;%u;%um", (bg >> 16) & 255, (bg >> 8) & 255,
               bg & 255);
        lbg = bg;
      }
      un = utf8_encode(cp, u);
      if (un > 0) fwrite(u, 1, (size_t)un, stdout);
      else fputc('?', stdout);
    }
  }
  fputs("\033[0m", stdout);
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
