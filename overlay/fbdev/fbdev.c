/* mote overlay/fbdev — Linux /dev/fb0 software framebuffer */
#include "platform.h"
#include "soft.h"
#include "soft_keys.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>

struct Plat {
  SoftFb fb;
  int fb_fd;
  void *fb_map;
  size_t fb_map_sz;
  int fb_w, fb_h, fb_bpp, fb_line;
  int kx, ky; /* crop origin when window < screen */
  int ev_fd;
  struct termios saved;
  mote_bool raw;
  char *clip;
  size_t clip_n;
  PlatEvent q[128];
  int qn;
  mote_bool ctrl, shift, alt, quit;
};

static void qpush(Plat *p, PlatEvent *e) {
  if (p->qn < (int)(sizeof p->q / sizeof p->q[0])) p->q[p->qn++] = *e;
}
static mote_bool qpop(Plat *p, PlatEvent *e) {
  if (p->qn <= 0) return MOTE_FALSE;
  *e = p->q[0];
  p->qn--;
  memmove(p->q, p->q + 1, (size_t)p->qn * sizeof p->q[0]);
  return MOTE_TRUE;
}
static void key_nav(Plat *p, PlatKey k) {
  PlatEvent e;
  memset(&e, 0, sizeof e);
  e.type = PE_KEY;
  e.key = k;
  e.ctrl = p->ctrl;
  e.shift = p->shift;
  qpush(p, &e);
}

static int open_keyboard(void) {
  DIR *d = opendir("/dev/input");
  struct dirent *e;
  int best = -1;
  if (!d) return -1;
  while ((e = readdir(d))) {
    char path[256];
    int fd;
    unsigned long bits[(EV_MAX + 1) / (8 * sizeof(long))];
    if (strncmp(e->d_name, "event", 5) != 0) continue;
    snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
    fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) continue;
    memset(bits, 0, sizeof bits);
    if (ioctl(fd, EVIOCGBIT(0, sizeof bits), bits) < 0) {
      close(fd);
      continue;
    }
    if (bits[EV_KEY / (8 * sizeof(long))] & (1UL << (EV_KEY % (8 * sizeof(long))))) {
      /* prefer keyboards: check KEY_A */
      unsigned long kbits[(KEY_MAX + 1) / (8 * sizeof(long))];
      memset(kbits, 0, sizeof kbits);
      if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof kbits), kbits) == 0) {
        if (kbits[KEY_A / (8 * sizeof(long))] & (1UL << (KEY_A % (8 * sizeof(long))))) {
          if (best >= 0) close(best);
          best = fd;
          continue;
        }
      }
    }
    close(fd);
  }
  closedir(d);
  return best;
}

static void tty_raw(Plat *p) {
  struct termios t;
  if (!isatty(STDIN_FILENO)) return;
  if (tcgetattr(STDIN_FILENO, &p->saved) != 0) return;
  t = p->saved;
  cfmakeraw(&t);
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
  p->raw = MOTE_TRUE;
}

static void map_linux_key(Plat *p, int code, int value) {
  if (code == KEY_LEFTCTRL || code == KEY_RIGHTCTRL) {
    p->ctrl = value != 0;
    return;
  }
  if (code == KEY_LEFTSHIFT || code == KEY_RIGHTSHIFT) {
    p->shift = value != 0;
    return;
  }
  if (code == KEY_LEFTALT || code == KEY_RIGHTALT) {
    p->alt = value != 0;
    return;
  }
  if (value != 1) return; /* press only */

  if (p->ctrl || p->alt) {
    PlatKey pk = PK_NONE;
    if (code >= KEY_A && code <= KEY_Z)
      pk = soft_ctrl_letter('A' + (code - KEY_A), p->shift, p->alt);
    else if (code == KEY_EQUAL)
      pk = PK_ZOOMIN;
    else if (code == KEY_MINUS)
      pk = PK_ZOOMOUT;
    else if (code == KEY_0)
      pk = PK_ZOOMRESET;
    else if (code == KEY_RIGHTBRACE)
      pk = PK_BRACKET;
    else if (code == KEY_TAB)
      pk = p->shift ? PK_PREVDOC : PK_NEXTDOC;
    if (pk != PK_NONE) {
      key_nav(p, pk);
      return;
    }
  }

  switch (code) {
  case KEY_LEFT: key_nav(p, PK_LEFT); return;
  case KEY_RIGHT: key_nav(p, PK_RIGHT); return;
  case KEY_UP: key_nav(p, PK_UP); return;
  case KEY_DOWN: key_nav(p, PK_DOWN); return;
  case KEY_HOME: key_nav(p, PK_HOME); return;
  case KEY_END: key_nav(p, PK_END); return;
  case KEY_PAGEUP: key_nav(p, PK_PGUP); return;
  case KEY_PAGEDOWN: key_nav(p, PK_PGDN); return;
  case KEY_BACKSPACE: key_nav(p, PK_BACKSPACE); return;
  case KEY_DELETE: key_nav(p, PK_DELETE); return;
  case KEY_ENTER: key_nav(p, PK_ENTER); return;
  case KEY_ESC: key_nav(p, PK_ESCAPE); return;
  case KEY_TAB: key_nav(p, PK_TAB); return;
  case KEY_F1: key_nav(p, PK_F1); return;
  case KEY_F3: key_nav(p, p->shift ? PK_FINDPREV : PK_FINDNEXT); return;
  case KEY_F4:
    if (p->ctrl) key_nav(p, PK_CLOSEDOC);
    return;
  case KEY_F5: key_nav(p, PK_RELOAD); return;
  case KEY_F7: key_nav(p, PK_WS); return;
  case KEY_Q:
    if (p->ctrl) {
      key_nav(p, PK_QUIT);
      return;
    }
    break;
  default: break;
  }

  if (p->ctrl || p->alt) return;
  /* crude US layout text */
  {
    static const char *row = "abcdefghijklmnopqrstuvwxyz";
    PlatEvent e;
    char ch = 0;
    if (code >= KEY_A && code <= KEY_Z) {
      ch = row[code - KEY_A];
      if (p->shift) ch = (char)(ch - 'a' + 'A');
    } else if (code == KEY_SPACE)
      ch = ' ';
    else if (code == KEY_1)
      ch = p->shift ? '!' : '1';
    else if (code == KEY_2)
      ch = p->shift ? '@' : '2';
    else if (code == KEY_3)
      ch = p->shift ? '#' : '3';
    else if (code == KEY_4)
      ch = p->shift ? '$' : '4';
    else if (code == KEY_5)
      ch = p->shift ? '%' : '5';
    else if (code == KEY_6)
      ch = p->shift ? '^' : '6';
    else if (code == KEY_7)
      ch = p->shift ? '&' : '7';
    else if (code == KEY_8)
      ch = p->shift ? '*' : '8';
    else if (code == KEY_9)
      ch = p->shift ? '(' : '9';
    else if (code == KEY_0)
      ch = p->shift ? ')' : '0';
    else if (code == KEY_MINUS)
      ch = p->shift ? '_' : '-';
    else if (code == KEY_EQUAL)
      ch = p->shift ? '+' : '=';
    else if (code == KEY_SEMICOLON)
      ch = p->shift ? ':' : ';';
    else if (code == KEY_APOSTROPHE)
      ch = p->shift ? '"' : '\'';
    else if (code == KEY_COMMA)
      ch = p->shift ? '<' : ',';
    else if (code == KEY_DOT)
      ch = p->shift ? '>' : '.';
    else if (code == KEY_SLASH)
      ch = p->shift ? '?' : '/';
    if (ch) {
      memset(&e, 0, sizeof e);
      e.type = PE_TEXT;
      e.text[0] = ch;
      e.text_len = 1;
      qpush(p, &e);
    }
  }
}

Plat *plat_create(const char *title, int w, int h) {
  Plat *p = (Plat *)calloc(1, sizeof(Plat));
  struct fb_var_screeninfo vinfo;
  struct fb_fix_screeninfo finfo;
  const char *dev;
  (void)title;
  if (!p) return NULL;
  p->fb_fd = -1;
  p->ev_fd = -1;
  soft_set_font_px(&p->fb, 16);
  if (!soft_resize(&p->fb, w, h)) {
    free(p);
    return NULL;
  }
  dev = getenv("MOTE_FB");
  if (!dev) dev = "/dev/fb0";
  p->fb_fd = open(dev, O_RDWR);
  if (p->fb_fd < 0) {
    free(p->fb.px);
    free(p);
    return NULL;
  }
  if (ioctl(p->fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
      ioctl(p->fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0) {
    plat_destroy(p);
    return NULL;
  }
  p->fb_w = (int)vinfo.xres;
  p->fb_h = (int)vinfo.yres;
  p->fb_bpp = (int)vinfo.bits_per_pixel;
  p->fb_line = (int)finfo.line_length;
  p->fb_map_sz = (size_t)finfo.smem_len;
  p->fb_map = mmap(NULL, p->fb_map_sz, PROT_READ | PROT_WRITE, MAP_SHARED, p->fb_fd, 0);
  if (p->fb_map == MAP_FAILED) {
    plat_destroy(p);
    return NULL;
  }
  if (p->fb.w > p->fb_w) soft_resize(&p->fb, p->fb_w, p->fb.h);
  if (p->fb.h > p->fb_h) soft_resize(&p->fb, p->fb.w, p->fb_h);
  p->kx = (p->fb_w - p->fb.w) / 2;
  p->ky = (p->fb_h - p->fb.h) / 2;
  if (p->kx < 0) p->kx = 0;
  if (p->ky < 0) p->ky = 0;
  p->ev_fd = open_keyboard();
  tty_raw(p);
  {
    PlatEvent e;
    memset(&e, 0, sizeof e);
    e.type = PE_EXPOSE;
    qpush(p, &e);
  }
  return p;
}

void plat_destroy(Plat *p) {
  if (!p) return;
  if (p->raw) tcsetattr(STDIN_FILENO, TCSANOW, &p->saved);
  free(p->clip);
  if (p->fb_map && p->fb_map != MAP_FAILED) munmap(p->fb_map, p->fb_map_sz);
  if (p->fb_fd >= 0) close(p->fb_fd);
  if (p->ev_fd >= 0) close(p->ev_fd);
  soft_free(&p->fb);
  free(p);
}

void plat_wait(Plat *p) {
  struct pollfd pf[2];
  int n = 0;
  if (p->qn > 0) return;
  if (p->ev_fd >= 0) {
    pf[n].fd = p->ev_fd;
    pf[n].events = POLLIN;
    n++;
  }
  if (isatty(STDIN_FILENO)) {
    pf[n].fd = STDIN_FILENO;
    pf[n].events = POLLIN;
    n++;
  }
  if (n > 0) poll(pf, (nfds_t)n, 50);
}

mote_bool plat_poll(Plat *p, PlatEvent *ev) {
  if (qpop(p, ev)) return MOTE_TRUE;
  if (p->ev_fd >= 0) {
    struct input_event ie;
    while (read(p->ev_fd, &ie, sizeof ie) == (ssize_t)sizeof ie) {
      if (ie.type == EV_KEY) map_linux_key(p, ie.code, ie.value);
    }
  }
  /* stdin escape for quit when no evdev */
  if (isatty(STDIN_FILENO)) {
    unsigned char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
      if (c == 3 || c == 'q') { /* Ctrl-C / q */
        p->quit = MOTE_TRUE;
        break;
      }
      if (c == 0x1b) {
        key_nav(p, PK_ESCAPE);
        break;
      }
    }
  }
  if (qpop(p, ev)) return MOTE_TRUE;
  if (p->quit) {
    ev->type = PE_QUIT;
    return MOTE_TRUE;
  }
  return MOTE_FALSE;
}

void plat_get_size(Plat *p, int *w, int *h) {
  *w = p->fb.w;
  *h = p->fb.h;
}
int plat_font_w(Plat *p) { return soft_font_w(&p->fb); }
int plat_font_h(Plat *p) { return soft_font_h(&p->fb); }
void plat_set_font_px(Plat *p, int px) { soft_set_font_px(&p->fb, px); }
int plat_font_px(Plat *p) { return p->fb.font_px; }
void plat_begin_frame(Plat *p) { (void)p; }
void plat_clear(Plat *p, mote_u32 rgb) { soft_clear(&p->fb, rgb); }
void plat_fill_rect(Plat *p, int x, int y, int w, int h, mote_u32 rgb) {
  soft_fill_rect(&p->fb, x, y, w, h, rgb);
}
void plat_draw_text(Plat *p, int x, int y, const char *s, int n, mote_u32 rgb) {
  soft_draw_text(&p->fb, x, y, s, n, rgb);
}
void plat_end_frame(Plat *p) {
  int y;
  soft_blit_caret(&p->fb);
  if (!p->fb_map || !p->fb.px) return;
  for (y = 0; y < p->fb.h; y++) {
    int dy = p->ky + y;
    if (dy < 0 || dy >= p->fb_h) continue;
    if (p->fb_bpp == 32) {
      memcpy((char *)p->fb_map + dy * p->fb_line + p->kx * 4,
             p->fb.px + (size_t)y * (size_t)p->fb.w, (size_t)p->fb.w * 4);
    } else if (p->fb_bpp == 16) {
      int x;
      unsigned short *dst =
          (unsigned short *)((char *)p->fb_map + dy * p->fb_line + p->kx * 2);
      for (x = 0; x < p->fb.w; x++) {
        mote_u32 c = p->fb.px[(size_t)y * (size_t)p->fb.w + (size_t)x];
        unsigned r = (c >> 16) & 0xFF, g = (c >> 8) & 0xFF, b = c & 0xFF;
        dst[x] = (unsigned short)(((r >> 3) << 11) | ((g >> 2) << 5) | (b >> 3));
      }
    }
  }
}
void plat_set_title(Plat *p, const char *title) { (void)p; (void)title; }
mote_bool plat_set_caret(Plat *p, int x, int y, int h, mote_bool on) {
  p->fb.caret_x = x;
  p->fb.caret_y = y;
  p->fb.caret_h = h;
  p->fb.caret_on = on;
  return MOTE_TRUE;
}
char *plat_clipboard_get(Plat *p, size_t *out_len) {
  if (out_len) *out_len = p->clip_n;
  if (!p->clip) return NULL;
  {
    char *c = (char *)malloc(p->clip_n + 1);
    if (!c) return NULL;
    memcpy(c, p->clip, p->clip_n);
    c[p->clip_n] = 0;
    return c;
  }
}
mote_bool plat_clipboard_set(Plat *p, const char *s, size_t n) {
  char *c = (char *)malloc(n + 1);
  if (!c) return MOTE_FALSE;
  memcpy(c, s, n);
  c[n] = 0;
  free(p->clip);
  p->clip = c;
  p->clip_n = n;
  return MOTE_TRUE;
}
