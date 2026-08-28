/* mote overlay/winconsole — Windows ConHost / VT console (MinGW) */
#include "platform.h"
#include "utf8.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

typedef struct {
  mote_u32 cp, fg, bg;
} Cell;

struct Plat {
  HANDLE hin, hout;
  int cols, rows, font_px, caret_x, caret_y, q_n, text_n;
  mote_bool caret_on, vt;
  DWORD in_mode_saved, out_mode_saved;
  UINT in_cp_saved, out_cp_saved;
  Cell *cells;
  Cell *prev; /* last painted (caret applied) — skip unchanged rows */
  CHAR_INFO *outbuf;
  char *clip;
  size_t clip_n;
  char *vtbuf;
  size_t vtbuf_n, vtbuf_cap;
  wchar_t *wtmp;
  int wtmp_cap;
  char text_acc[32];
  PlatEvent q[256];
};

/* Palette tuned so nearest-16 mapping keeps syntax HL distinct. */
static const mote_u32 VGA16[16] = {
    0x0F1419ul, 0x007ACCul, 0x6A9955ul, 0x4EC9B0ul, 0xCE9178ul, 0xC586C0ul,
    0xD7BA7Dul, 0xD4D4D4ul, 0x5C6773ul, 0x569CD6ul, 0xB5CEA8ul, 0x39BAE6ul,
    0xF44747ul, 0xD4BFFFul, 0xFFD700ul, 0xFFFFFFul};

static WORD nearest_attr_fg(mote_u32 rgb) {
  int i, best = 7;
  long best_d = 0x7fffffffL;
  int r = (int)((rgb >> 16) & 255), g = (int)((rgb >> 8) & 255),
      b = (int)(rgb & 255);
  static const WORD map[16] = {
      0, FOREGROUND_BLUE, FOREGROUND_GREEN, FOREGROUND_GREEN | FOREGROUND_BLUE,
      FOREGROUND_RED, FOREGROUND_RED | FOREGROUND_BLUE,
      FOREGROUND_RED | FOREGROUND_GREEN,
      FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE,
      FOREGROUND_INTENSITY,
      FOREGROUND_BLUE | FOREGROUND_INTENSITY,
      FOREGROUND_GREEN | FOREGROUND_INTENSITY,
      FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
      FOREGROUND_RED | FOREGROUND_INTENSITY,
      FOREGROUND_RED | FOREGROUND_BLUE | FOREGROUND_INTENSITY,
      FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_INTENSITY,
      FOREGROUND_RED | FOREGROUND_GREEN | FOREGROUND_BLUE | FOREGROUND_INTENSITY};
  for (i = 0; i < 16; i++) {
    int vr = (int)((VGA16[i] >> 16) & 255);
    int vg = (int)((VGA16[i] >> 8) & 255);
    int vb = (int)(VGA16[i] & 255);
    long dr = r - vr, dg = g - vg, db = b - vb;
    long d = dr * dr + dg * dg + db * db;
    long dbri = (r + g + b) - (vr + vg + vb);
    d += (dbri * dbri) / 4;
    if (d < best_d) {
      best_d = d;
      best = i;
    }
  }
  return map[best];
}

static WORD nearest_attr_bg(mote_u32 rgb) {
  /* reuse FG mapper bits shifted to background */
  WORD fg = nearest_attr_fg(rgb);
  WORD bg = 0;
  if (fg & FOREGROUND_BLUE) bg |= BACKGROUND_BLUE;
  if (fg & FOREGROUND_GREEN) bg |= BACKGROUND_GREEN;
  if (fg & FOREGROUND_RED) bg |= BACKGROUND_RED;
  if (fg & FOREGROUND_INTENSITY) bg |= BACKGROUND_INTENSITY;
  return bg;
}

/* Match host buffer to the visible window so the UI (incl. status) is on-screen. */
static void sync_host_window(Plat *p) {
  CONSOLE_SCREEN_BUFFER_INFO info;
  COORD buf;
  SMALL_RECT win;
  if (!GetConsoleScreenBufferInfo(p->hout, &info)) return;
  buf.X = (SHORT)p->cols;
  buf.Y = (SHORT)p->rows;
  /* Window must fit inside buffer: shrink window first if needed, then grow buffer. */
  win.Left = 0;
  win.Top = 0;
  win.Right = (SHORT)(p->cols - 1);
  win.Bottom = (SHORT)(p->rows - 1);
  if (info.dwSize.X < buf.X || info.dwSize.Y < buf.Y) {
    COORD big;
    big.X = (SHORT)(buf.X > info.dwSize.X ? buf.X : info.dwSize.X);
    big.Y = (SHORT)(buf.Y > info.dwSize.Y ? buf.Y : info.dwSize.Y);
    SetConsoleScreenBufferSize(p->hout, big);
  }
  SetConsoleWindowInfo(p->hout, TRUE, &win);
  SetConsoleScreenBufferSize(p->hout, buf);
  SetConsoleWindowInfo(p->hout, TRUE, &win);
  {
    COORD origin;
    origin.X = 0;
    origin.Y = 0;
    SetConsoleCursorPosition(p->hout, origin);
  }
}

static int idx(Plat *p, int x, int y) { return y * p->cols + x; }

static mote_bool resize(Plat *p, int cols, int rows) {
  Cell *c, *prev;
  CHAR_INFO *o;
  size_t n, i;
  if (cols < 40) cols = 40;
  if (rows < 10) rows = 10;
  if (cols > 300) cols = 300;
  if (rows > 120) rows = 120;
  n = (size_t)cols * (size_t)rows;
  c = (Cell *)calloc(n, sizeof(Cell));
  prev = (Cell *)calloc(n, sizeof(Cell));
  o = (CHAR_INFO *)calloc(n, sizeof(CHAR_INFO));
  if (!c || !prev || !o) {
    free(c);
    free(prev);
    free(o);
    return MOTE_FALSE;
  }
  free(p->cells);
  free(p->prev);
  free(p->outbuf);
  p->cells = c;
  p->prev = prev;
  p->outbuf = o;
  p->cols = cols;
  p->rows = rows;
  /* Force full repaint after resize. */
  for (i = 0; i < n; i++) {
    p->prev[i].cp = (mote_u32)~0u;
    p->prev[i].fg = 0;
    p->prev[i].bg = 0;
  }
  sync_host_window(p);
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

static void key_flush(Plat *p, PlatKey k, mote_bool ctrl, mote_bool shift) {
  text_flush(p);
  key(p, k, ctrl, shift);
}

static void ctrl_letter(Plat *p, int vk, mote_bool shift) {
  PlatKey k = PK_NONE;
  switch (vk) {
  case 'S': k = shift ? PK_SAVEAS : PK_SAVE; break;
  case 'O': k = PK_OPEN; break;
  case 'Q': k = PK_QUIT; break;
  case 'Z': k = PK_UNDO; break;
  case 'Y': k = PK_REDO; break;
  case 'F': k = PK_FIND; break;
  case 'G': k = PK_GOTO; break;
  case 'R': k = shift ? PK_READONLY : PK_REPLACE; break;
  case 'X': k = PK_CUT; break;
  case 'C': k = PK_COPY; break;
  case 'V': k = PK_PASTE; break;
  case 'A': k = PK_SELALL; break;
  case 'T': k = PK_THEME; break;
  case 'W': k = shift ? PK_CLOSEDOC : PK_WRAP; break;
  case 'D': k = PK_DUPLINE; break;
  case 'N': k = PK_NEWDOC; break;
  case 'E': k = shift ? PK_EOL : PK_RECENT; break;
  case 'K':
    if (shift) k = PK_DELLINE;
    break;
  case 'P': k = shift ? PK_BOOKMARK_SET : PK_QUICKOPEN; break;
  case VK_OEM_2: k = PK_COMMENT; break;
  case VK_OEM_PLUS: k = PK_ZOOMIN; break;
  case VK_OEM_MINUS: k = PK_ZOOMOUT; break;
  case '0': k = PK_ZOOMRESET; break;
  case VK_OEM_6: k = PK_BRACKET; break;
  default: break;
  }
  if (k != PK_NONE) key_flush(p, k, MOTE_TRUE, shift);
}

static void ingest_key_event(Plat *p, KEY_EVENT_RECORD *ke) {
  mote_bool ctrl, shift, alt;
  WORD vk;
  WCHAR ch;
  if (!ke->bKeyDown) return;
  ctrl = (ke->dwControlKeyState & (LEFT_CTRL_PRESSED | RIGHT_CTRL_PRESSED)) != 0;
  shift = (ke->dwControlKeyState & SHIFT_PRESSED) != 0;
  alt = (ke->dwControlKeyState & (LEFT_ALT_PRESSED | RIGHT_ALT_PRESSED)) != 0;
  vk = ke->wVirtualKeyCode;
  ch = ke->uChar.UnicodeChar;

  if (alt && !ctrl) {
    PlatKey ak = PK_NONE;
    if (vk == 'H' || vk == 'h') ak = PK_HELP;
    else if (vk == 'C' || vk == 'c') ak = PK_FINDCASE;
    else if (vk == 'W' || vk == 'w') ak = PK_FINDWORD;
    else if (vk == 'S' || vk == 's') ak = PK_SAVEAS;
    else if (vk == 'R' || vk == 'r') ak = PK_READONLY;
    else if (vk == 'K' || vk == 'k') ak = PK_DELLINE;
    else if (vk == 'E' || vk == 'e') ak = PK_EOL;
    else if (vk == 'N' || vk == 'n') ak = PK_NEXTDOC;
    else if (vk == 'P' || vk == 'p') ak = PK_PREVDOC;
    else if (vk == 'M' || vk == 'm') ak = PK_BOOKMARK_SET;
    else if (vk == 'J' || vk == 'j') ak = PK_BOOKMARK;
    if (ak != PK_NONE) {
      key_flush(p, ak, MOTE_FALSE, MOTE_FALSE);
      return;
    }
  }
  if (ctrl && vk == VK_TAB) {
    key_flush(p, shift ? PK_PREVDOC : PK_NEXTDOC, MOTE_TRUE, shift);
    return;
  }
  if (ctrl && vk == VK_F4) {
    key_flush(p, PK_CLOSEDOC, MOTE_TRUE, MOTE_FALSE);
    return;
  }
  if (ctrl) {
    ctrl_letter(p, (int)vk, shift);
    return;
  }

  switch (vk) {
  case VK_LEFT: key_flush(p, PK_LEFT, MOTE_FALSE, shift); return;
  case VK_RIGHT: key_flush(p, PK_RIGHT, MOTE_FALSE, shift); return;
  case VK_UP: key_flush(p, PK_UP, MOTE_FALSE, shift); return;
  case VK_DOWN: key_flush(p, PK_DOWN, MOTE_FALSE, shift); return;
  case VK_HOME: key_flush(p, PK_HOME, MOTE_FALSE, shift); return;
  case VK_END: key_flush(p, PK_END, MOTE_FALSE, shift); return;
  case VK_PRIOR: key_flush(p, PK_PGUP, MOTE_FALSE, shift); return;
  case VK_NEXT: key_flush(p, PK_PGDN, MOTE_FALSE, shift); return;
  case VK_BACK: key_flush(p, PK_BACKSPACE, MOTE_FALSE, MOTE_FALSE); return;
  case VK_DELETE: key_flush(p, PK_DELETE, MOTE_FALSE, MOTE_FALSE); return;
  case VK_RETURN:
    if (ctrl && shift) key_flush(p, PK_BOOKMARK_SET, MOTE_TRUE, MOTE_TRUE);
    else if (ctrl) key_flush(p, PK_BOOKMARK, MOTE_TRUE, MOTE_FALSE);
    else key_flush(p, PK_ENTER, MOTE_FALSE, MOTE_FALSE);
    return;
  case VK_ESCAPE: key_flush(p, PK_ESCAPE, MOTE_FALSE, MOTE_FALSE); return;
  case VK_TAB: key_flush(p, PK_TAB, MOTE_FALSE, shift); return;
  case VK_F1: key_flush(p, PK_HELP, MOTE_FALSE, MOTE_FALSE); return;
  case VK_F2: key_flush(p, shift ? PK_PREVDOC : PK_NEXTDOC, MOTE_FALSE, shift); return;
  case VK_F3: key_flush(p, shift ? PK_FINDPREV : PK_FINDNEXT, MOTE_FALSE, shift); return;
  case VK_F5: key_flush(p, PK_RELOAD, MOTE_FALSE, MOTE_FALSE); return;
  case VK_F7: key_flush(p, PK_WS, MOTE_FALSE, MOTE_FALSE); return;
  default: break;
  }

  if (ch >= 32) {
    char utf[8];
    int n;
    /* BMP only for simplicity */
    if (ch < 128) {
      char c = (char)ch;
      text_add(p, &c, 1);
    } else {
      n = utf8_encode((mote_u32)ch, utf);
      if (n > 0) text_add(p, utf, n);
    }
  }
}

static void poll_console_size(Plat *p) {
  CONSOLE_SCREEN_BUFFER_INFO info;
  int cols, rows;
  if (!GetConsoleScreenBufferInfo(p->hout, &info)) return;
  cols = info.srWindow.Right - info.srWindow.Left + 1;
  rows = info.srWindow.Bottom - info.srWindow.Top + 1;
  if (cols != p->cols || rows != p->rows) {
    PlatEvent e;
    if (resize(p, cols, rows)) {
      memset(&e, 0, sizeof e);
      e.type = PE_EXPOSE;
      qpush(p, &e);
    }
  }
}

static int running_on_wine(void) {
  HMODULE nt = GetModuleHandleA("ntdll.dll");
  return nt && GetProcAddress(nt, "wine_get_version") != NULL;
}

/* Raster OEM fonts lack Cyrillic; prefer a TrueType face with BMP coverage. */
static void prefer_unicode_font(HANDLE hout) {
  CONSOLE_FONT_INFOEX fi, set;
  static const wchar_t *names[] = {L"Cascadia Mono", L"Consolas",
                                   L"Lucida Console", L"Courier New", NULL};
  int i;
  memset(&fi, 0, sizeof fi);
  fi.cbSize = sizeof fi;
  if (!GetCurrentConsoleFontEx(hout, FALSE, &fi)) return;
  for (i = 0; names[i]; i++) {
    set = fi;
    memset(set.FaceName, 0, sizeof set.FaceName);
    {
      const wchar_t *s = names[i];
      int j;
      for (j = 0; j < LF_FACESIZE - 1 && s[j]; j++) set.FaceName[j] = s[j];
    }
    if (SetCurrentConsoleFontEx(hout, FALSE, &set)) return;
  }
}

/* Optional: dump cell grid for gallery (MOTE_DUMP_CELLS=path.cells). */
static void dump_cells_file(Plat *p) {
  const char *path;
  FILE *f;
  int i, n;
  static int dumped;
  if (dumped) return;
  path = getenv("MOTE_DUMP_CELLS");
  if (!path || !path[0] || !p->cells) return;
  dumped = 1;
  f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "MOTECELL %d %d\n", p->cols, p->rows);
  n = p->cols * p->rows;
  for (i = 0; i < n; i++) {
    Cell *c = &p->cells[i];
    unsigned char b[12];
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

Plat *plat_create(const char *title, int w, int h) {
  Plat *p;
  DWORD om = 0, im = 0;
  int cols = 80, rows = 25;
  CONSOLE_SCREEN_BUFFER_INFO info;
  p = (Plat *)calloc(1, sizeof *p);
  if (!p) return NULL;
  p->font_px = 16;
  p->hin = GetStdHandle(STD_INPUT_HANDLE);
  p->hout = GetStdHandle(STD_OUTPUT_HANDLE);
  if (p->hin == INVALID_HANDLE_VALUE || p->hout == INVALID_HANDLE_VALUE) {
    free(p);
    return NULL;
  }
  GetConsoleMode(p->hin, &p->in_mode_saved);
  GetConsoleMode(p->hout, &p->out_mode_saved);
  p->in_cp_saved = GetConsoleCP();
  p->out_cp_saved = GetConsoleOutputCP();
  prefer_unicode_font(p->hout);
  im = p->in_mode_saved;
  im &= ~(ENABLE_ECHO_INPUT | ENABLE_LINE_INPUT | ENABLE_PROCESSED_INPUT);
  im |= ENABLE_WINDOW_INPUT | ENABLE_MOUSE_INPUT;
  SetConsoleMode(p->hin, im);
  om = p->out_mode_saved;
  om |= ENABLE_PROCESSED_OUTPUT;
  /*
   * VT truecolor is slow (escape storm, esp. with whitespace dots) and often
   * mangled Cyrillic via WriteConsole. Default: CHAR_INFO + WriteConsoleOutputW.
   * Opt in: MOTE_VT=1
   */
  p->vt = MOTE_FALSE;
#ifdef ENABLE_VIRTUAL_TERMINAL_PROCESSING
  if (!running_on_wine() && getenv("MOTE_VT") && getenv("MOTE_VT")[0] == '1') {
    om |= ENABLE_VIRTUAL_TERMINAL_PROCESSING;
    SetConsoleMode(p->hout, om);
    {
      DWORD m = 0;
      if (GetConsoleMode(p->hout, &m) && (m & ENABLE_VIRTUAL_TERMINAL_PROCESSING)) {
        p->vt = MOTE_TRUE;
        SetConsoleCP(CP_UTF8);
        SetConsoleOutputCP(CP_UTF8);
      }
    }
  } else
#endif
  {
    SetConsoleMode(p->hout, om);
  }
  if (getenv("MOTE_NO_VT")) p->vt = MOTE_FALSE;
  if (title && title[0]) {
    wchar_t wt[128];
    MultiByteToWideChar(CP_UTF8, 0, title, -1, wt, 128);
    SetConsoleTitleW(wt);
  }
  if (GetConsoleScreenBufferInfo(p->hout, &info)) {
    cols = info.srWindow.Right - info.srWindow.Left + 1;
    rows = info.srWindow.Bottom - info.srWindow.Top + 1;
  }
  if (w >= 40 && w <= 300) cols = w;
  if (h >= 10 && h <= 120) rows = h;
  if (!resize(p, cols, rows)) {
    free(p);
    return NULL;
  }
  return p;
}

void plat_destroy(Plat *p) {
  if (!p) return;
  SetConsoleMode(p->hin, p->in_mode_saved);
  SetConsoleMode(p->hout, p->out_mode_saved);
  if (p->in_cp_saved) SetConsoleCP(p->in_cp_saved);
  if (p->out_cp_saved) SetConsoleOutputCP(p->out_cp_saved);
  free(p->clip);
  free(p->cells);
  free(p->prev);
  free(p->outbuf);
  free(p->vtbuf);
  free(p->wtmp);
  free(p);
}

void plat_wait(Plat *p) {
  text_flush(p);
  poll_console_size(p);
  if (p->q_n > 0) return;
  WaitForSingleObject(p->hin, INFINITE);
}

mote_bool plat_poll(Plat *p, PlatEvent *ev) {
  INPUT_RECORD rec[32];
  DWORD n = 0, i;
  memset(ev, 0, sizeof *ev);
  poll_console_size(p);
  if (qpop(p, ev)) return MOTE_TRUE;
  if (!PeekConsoleInputW(p->hin, rec, 1, &n) || n == 0) {
    text_flush(p);
    return qpop(p, ev);
  }
  if (!ReadConsoleInputW(p->hin, rec, 32, &n)) return MOTE_FALSE;
  for (i = 0; i < n; i++) {
    if (rec[i].EventType == KEY_EVENT)
      ingest_key_event(p, &rec[i].Event.KeyEvent);
    else if (rec[i].EventType == WINDOW_BUFFER_SIZE_EVENT)
      poll_console_size(p);
  }
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

static void vt_write(Plat *p, const char *s, size_t n) {
  if (n == 0) return;
  if (p->vtbuf_n + n > p->vtbuf_cap) {
    size_t nc = p->vtbuf_cap ? p->vtbuf_cap * 2 : 16384;
    char *nb;
    while (nc < p->vtbuf_n + n) nc *= 2;
    nb = (char *)realloc(p->vtbuf, nc);
    if (!nb) return;
    p->vtbuf = nb;
    p->vtbuf_cap = nc;
  }
  memcpy(p->vtbuf + p->vtbuf_n, s, n);
  p->vtbuf_n += n;
}

/* UTF-8 → UTF-16 via WriteConsoleW: reliable Cyrillic with VT enabled. */
static void vt_flush(Plat *p) {
  int wn;
  DWORD written = 0;
  if (!p->vtbuf_n) return;
  wn = MultiByteToWideChar(CP_UTF8, 0, p->vtbuf, (int)p->vtbuf_n, NULL, 0);
  if (wn <= 0) {
    p->vtbuf_n = 0;
    return;
  }
  if (wn > p->wtmp_cap) {
    wchar_t *nw = (wchar_t *)realloc(p->wtmp, (size_t)wn * sizeof(wchar_t));
    if (!nw) {
      p->vtbuf_n = 0;
      return;
    }
    p->wtmp = nw;
    p->wtmp_cap = wn;
  }
  MultiByteToWideChar(CP_UTF8, 0, p->vtbuf, (int)p->vtbuf_n, p->wtmp, wn);
  WriteConsoleW(p->hout, p->wtmp, (DWORD)wn, &written, NULL);
  p->vtbuf_n = 0;
}

static void cell_paint(Plat *p, int x, int y, Cell *out) {
  Cell *c = &p->cells[idx(p, x, y)];
  out->cp = c->cp ? c->cp : (mote_u32)' ';
  out->fg = c->fg;
  out->bg = c->bg;
  if (p->caret_on && x == p->caret_x && y == p->caret_y) {
    mote_u32 t = out->fg;
    out->fg = out->bg;
    out->bg = t;
  }
}

static int row_dirty(Plat *p, int y) {
  int x;
  for (x = 0; x < p->cols; x++) {
    Cell cur;
    Cell *old;
    cell_paint(p, x, y, &cur);
    old = &p->prev[idx(p, x, y)];
    if (cur.cp != old->cp || cur.fg != old->fg || cur.bg != old->bg) return 1;
  }
  return 0;
}

static void row_commit(Plat *p, int y) {
  int x;
  for (x = 0; x < p->cols; x++) {
    Cell cur;
    cell_paint(p, x, y, &cur);
    p->prev[idx(p, x, y)] = cur;
  }
}

void plat_end_frame(Plat *p) {
  COORD bufSize, bufCoord;
  SMALL_RECT region;
  CONSOLE_SCREEN_BUFFER_INFO info;
  int y, x;
  SHORT left = 0, top = 0;

  dump_cells_file(p);
  if (getenv("MOTE_SHOT_ONCE")) {
    /* First painted frame is enough for gallery dumps. */
    ExitProcess(0);
  }

  if (p->vt) {
    char esc[64];
    int any = 0;
    p->vtbuf_n = 0;
    vt_write(p, "\033[?25l", 6);
    for (y = 0; y < p->rows; y++) {
      mote_u32 lfg = ~0ul, lbg = ~0ul;
      if (!row_dirty(p, y)) continue;
      any = 1;
      {
        int el = snprintf(esc, sizeof esc, "\033[%d;1H", y + 1);
        if (el > 0) vt_write(p, esc, (size_t)el);
      }
      for (x = 0; x < p->cols; x++) {
        Cell cur;
        char u[8];
        int un, el;
        cell_paint(p, x, y, &cur);
        if (cur.fg != lfg) {
          el = snprintf(esc, sizeof esc, "\033[38;2;%lu;%lu;%lum",
                        (unsigned long)((cur.fg >> 16) & 255),
                        (unsigned long)((cur.fg >> 8) & 255),
                        (unsigned long)(cur.fg & 255));
          if (el > 0) vt_write(p, esc, (size_t)el);
          lfg = cur.fg;
        }
        if (cur.bg != lbg) {
          el = snprintf(esc, sizeof esc, "\033[48;2;%lu;%lu;%lum",
                        (unsigned long)((cur.bg >> 16) & 255),
                        (unsigned long)((cur.bg >> 8) & 255),
                        (unsigned long)(cur.bg & 255));
          if (el > 0) vt_write(p, esc, (size_t)el);
          lbg = cur.bg;
        }
        un = utf8_encode(cur.cp, u);
        if (un > 0) vt_write(p, u, (size_t)un);
        else vt_write(p, "?", 1);
      }
      row_commit(p, y);
    }
    if (any) vt_write(p, "\033[0m", 4);
    vt_flush(p);
    return;
  }

  if (GetConsoleScreenBufferInfo(p->hout, &info)) {
    left = info.srWindow.Left;
    top = info.srWindow.Top;
    if (left != 0 || top != 0 ||
        info.dwSize.X != (SHORT)p->cols || info.dwSize.Y != (SHORT)p->rows) {
      sync_host_window(p);
      left = 0;
      top = 0;
    }
  }

  /* Dirty-row WriteConsoleOutputW — Unicode BMP (Cyrillic) intact. */
  for (y = 0; y < p->rows; y++) {
    CHAR_INFO *row;
    if (!row_dirty(p, y)) continue;
    row = &p->outbuf[idx(p, 0, y)];
    for (x = 0; x < p->cols; x++) {
      Cell cur;
      WCHAR wch;
      WORD attr;
      cell_paint(p, x, y, &cur);
      attr = nearest_attr_fg(cur.fg) | nearest_attr_bg(cur.bg);
      if (cur.cp <= 0xFFFF)
        wch = (WCHAR)cur.cp;
      else
        wch = L'?';
      row[x].Char.UnicodeChar = wch;
      row[x].Attributes = attr;
    }
    bufSize.X = (SHORT)p->cols;
    bufSize.Y = 1;
    bufCoord.X = 0;
    bufCoord.Y = 0;
    region.Left = left;
    region.Top = (SHORT)(top + y);
    region.Right = (SHORT)(left + p->cols - 1);
    region.Bottom = (SHORT)(top + y);
    WriteConsoleOutputW(p->hout, row, bufSize, bufCoord, &region);
    row_commit(p, y);
  }
}

void plat_set_title(Plat *p, const char *title) {
  wchar_t wt[256];
  (void)p;
  if (!title) return;
  MultiByteToWideChar(CP_UTF8, 0, title, -1, wt, 256);
  SetConsoleTitleW(wt);
}

mote_bool plat_set_caret(Plat *p, int x, int y, int h, mote_bool on) {
  (void)h;
  p->caret_x = x;
  p->caret_y = y;
  p->caret_on = on;
  return MOTE_TRUE;
}

char *plat_clipboard_get(Plat *p, size_t *out_len) {
  HANDLE h;
  wchar_t *w;
  int n;
  char *u;
  (void)p;
  if (!OpenClipboard(NULL)) {
    if (out_len) *out_len = 0;
    return NULL;
  }
  h = GetClipboardData(CF_UNICODETEXT);
  if (!h) {
    CloseClipboard();
    if (out_len) *out_len = 0;
    return NULL;
  }
  w = (wchar_t *)GlobalLock(h);
  if (!w) {
    CloseClipboard();
    return NULL;
  }
  n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
  u = (char *)malloc((size_t)n);
  if (u) WideCharToMultiByte(CP_UTF8, 0, w, -1, u, n, NULL, NULL);
  GlobalUnlock(h);
  CloseClipboard();
  if (u && out_len) *out_len = strlen(u);
  return u;
}

mote_bool plat_clipboard_set(Plat *p, const char *s, size_t n) {
  HGLOBAL h;
  wchar_t *w;
  int wn;
  char *tmp;
  (void)p;
  tmp = (char *)malloc(n + 1);
  if (!tmp) return MOTE_FALSE;
  memcpy(tmp, s, n);
  tmp[n] = 0;
  wn = MultiByteToWideChar(CP_UTF8, 0, tmp, -1, NULL, 0);
  free(tmp);
  if (wn <= 0) return MOTE_FALSE;
  if (!OpenClipboard(NULL)) return MOTE_FALSE;
  EmptyClipboard();
  h = GlobalAlloc(GMEM_MOVEABLE, (SIZE_T)wn * sizeof(wchar_t));
  if (!h) {
    CloseClipboard();
    return MOTE_FALSE;
  }
  w = (wchar_t *)GlobalLock(h);
  tmp = (char *)malloc(n + 1);
  if (!tmp || !w) {
    free(tmp);
    GlobalUnlock(h);
    GlobalFree(h);
    CloseClipboard();
    return MOTE_FALSE;
  }
  memcpy(tmp, s, n);
  tmp[n] = 0;
  MultiByteToWideChar(CP_UTF8, 0, tmp, -1, w, wn);
  free(tmp);
  GlobalUnlock(h);
  SetClipboardData(CF_UNICODETEXT, h);
  CloseClipboard();
  return MOTE_TRUE;
}
