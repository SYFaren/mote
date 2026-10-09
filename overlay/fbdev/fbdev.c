#include "platform.h"
#include "soft.h"
#include "../evq.h"
#include "keymap.h"

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
  EvQueue q;
  mote_bool ctrl, shift, alt;
};

static void key_nav(Plat *p, PlatKey k) {
  evq_key(&p->q, k, p->ctrl, p->shift);
}

#define LONG_BITS (8 * sizeof(long))
#define NLONGS(n) (((n) + LONG_BITS - 1) / LONG_BITS)
#define TEST_BIT(arr, b) (((arr)[(b) / LONG_BITS] >> ((b) % LONG_BITS)) & 1UL)

/* First /dev/input/event* that has letter keys. */
static int open_keyboard(void) {
  DIR *d = opendir("/dev/input");
  struct dirent *e;
  int found = -1;
  if (!d) return -1;
  while (found < 0 && (e = readdir(d))) {
    char path[256];
    unsigned long ev[NLONGS(EV_MAX + 1)], keys[NLONGS(KEY_MAX + 1)];
    int fd;
    if (strncmp(e->d_name, "event", 5) != 0) continue;
    snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
    fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) continue;
    memset(ev, 0, sizeof ev);
    memset(keys, 0, sizeof keys);
    if (ioctl(fd, EVIOCGBIT(0, sizeof ev), ev) >= 0 && TEST_BIT(ev, EV_KEY) &&
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) >= 0 && TEST_BIT(keys, KEY_A))
      found = fd;
    else
      close(fd);
  }
  closedir(d);
  return found;
}

static void tty_raw(Plat *p) {
  struct termios t;
  if (!isatty(STDIN_FILENO)) return;
  if (tcgetattr(STDIN_FILENO, &p->saved) != 0) return;
  t = p->saved;
  cfmakeraw(&t);
  t.c_cc[VMIN] = 0; /* plat_poll drains stdin and must not block */
  t.c_cc[VTIME] = 0;
  tcsetattr(STDIN_FILENO, TCSANOW, &t);
  p->raw = MOTE_TRUE;
}

/* US layout indexed by evdev code (KEY_1 = 2 ... KEY_SPACE = 57); evdev
   numbers keys in keyboard-row order, not alphabetically. */
static const char US_LO[] = "\0\0" "1234567890-=" "\0\0" "qwertyuiop[]" "\0\0"
                            "asdfghjkl;'`" "\0\\" "zxcvbnm,./" "\0*\0 ";
static const char US_HI[] = "\0\0" "!@#$%^&*()_+" "\0\0" "QWERTYUIOP{}" "\0\0"
                            "ASDFGHJKL:\"~" "\0|" "ZXCVBNM<>?" "\0*\0 ";

static int us_char(int code, mote_bool shift) {
  switch (code) {
  case KEY_TAB: return '\t';
  case KEY_ENTER:
  case KEY_KPENTER: return '\r';
  case KEY_KPPLUS: return '+';
  case KEY_KPMINUS: return '-';
  case KEY_KP0: return '0';
  default: break;
  }
  if (code < 0 || code >= (int)sizeof US_LO - 1) return 0;
  return shift ? US_HI[code] : US_LO[code];
}

static int fn_number(int code) {
  if (code >= KEY_F1 && code <= KEY_F10) return code - KEY_F1 + 1;
  if (code == KEY_F11) return 11;
  if (code == KEY_F12) return 12;
  return 0;
}

static PlatKey nav_key(int code) {
  switch (code) {
  case KEY_LEFT: return PK_LEFT;
  case KEY_RIGHT: return PK_RIGHT;
  case KEY_UP: return PK_UP;
  case KEY_DOWN: return PK_DOWN;
  case KEY_HOME: return PK_HOME;
  case KEY_END: return PK_END;
  case KEY_PAGEUP: return PK_PGUP;
  case KEY_PAGEDOWN: return PK_PGDN;
  case KEY_BACKSPACE: return PK_BACKSPACE;
  case KEY_DELETE: return PK_DELETE;
  case KEY_ENTER:
  case KEY_KPENTER: return PK_ENTER;
  case KEY_ESC: return PK_ESCAPE;
  case KEY_TAB: return PK_TAB;
  default: return PK_NONE;
  }
}

/* value: 0 release, 1 press, 2 autorepeat */
static void map_linux_key(Plat *p, int code, int value) {
  PlatKey pk = PK_NONE;
  int ch;
  switch (code) {
  case KEY_LEFTCTRL:
  case KEY_RIGHTCTRL: p->ctrl = value != 0; return;
  case KEY_LEFTSHIFT:
  case KEY_RIGHTSHIFT: p->shift = value != 0; return;
  case KEY_LEFTALT:
  case KEY_RIGHTALT: p->alt = value != 0; return;
  default: break;
  }
  if (value == 0) return;
  ch = us_char(code, MOTE_FALSE);
  if (p->alt && !p->ctrl && ch) pk = key_alt(ch);
  else if (p->ctrl && ch) pk = key_ctrl(ch, p->shift);
  if (pk == PK_NONE) pk = nav_key(code);
  if (pk == PK_NONE && fn_number(code)) pk = key_fn(fn_number(code), p->ctrl, p->shift);
  if (pk != PK_NONE) {
    key_nav(p, pk);
    return;
  }
  ch = us_char(code, p->shift);
  if (!p->ctrl && !p->alt && ch >= ' ') {
    char c = (char)ch;
    evq_text(&p->q, &c, 1);
    evq_flush_text(&p->q);
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
  soft_set_font_px(&p->fb, MOTE_FONT_PX);
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
  evq_type(&p->q, PE_EXPOSE);
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
  if (p->q.n > 0) return;
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
  if (evq_pop(&p->q, ev)) return MOTE_TRUE;
  if (p->ev_fd >= 0) {
    struct input_event ie;
    while (read(p->ev_fd, &ie, sizeof ie) == (ssize_t)sizeof ie) {
      if (ie.type == EV_KEY) map_linux_key(p, ie.code, ie.value);
    }
  }
  /* The VT still queues every key we read from evdev: drain it. Without a
     keyboard device it is the only input, good for Esc and Ctrl+C / Ctrl+Q. */
  if (isatty(STDIN_FILENO)) {
    unsigned char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
      if (p->ev_fd >= 0) continue;
      if (c == 3 || c == 17) evq_key(&p->q, PK_QUIT, MOTE_TRUE, MOTE_FALSE);
      else if (c == 0x1b) key_nav(p, PK_ESCAPE);
    }
  }
  return evq_pop(&p->q, ev);
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
