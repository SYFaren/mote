#include "platform.h"
#include "soft.h"
#include "../evq.h"
#include "keymap.h"

#include <errno.h>
#include <fcntl.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/kd.h>
#include <linux/major.h>
#include <linux/vt.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/sysmacros.h>
#include <termios.h>
#include <unistd.h>
#include <dirent.h>

#define MAX_KBD 8

struct Plat {
  SoftFb fb;
  int fb_fd;
  void *fb_map;
  size_t fb_map_sz;
  int fb_w, fb_h, fb_bpp, fb_line;
  int fb_r, fb_g, fb_b; /* channel bit offsets */
  int kx, ky; /* crop origin when window < screen */
  int ev_fd[MAX_KBD];
  int nev;
  int vt; /* our console number, 0 if unknown */
  mote_bool away; /* another console is showing */
  struct termios saved;
  mote_bool raw, graphics;
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

static mote_bool has_letter_keys(int fd) {
  unsigned long ev[NLONGS(EV_MAX + 1)], keys[NLONGS(KEY_MAX + 1)];
  memset(ev, 0, sizeof ev);
  memset(keys, 0, sizeof keys);
  return ioctl(fd, EVIOCGBIT(0, sizeof ev), ev) >= 0 && TEST_BIT(ev, EV_KEY) &&
         ioctl(fd, EVIOCGBIT(EV_KEY, sizeof keys), keys) >= 0 && TEST_BIT(keys, KEY_A) &&
         TEST_BIT(keys, KEY_Z) && TEST_BIT(keys, KEY_SPACE) && TEST_BIT(keys, KEY_ENTER);
}

/* Every /dev/input/event* with letter keys. Gaming mice claim a whole
   keyboard too and one keyboard can be split over several devices, so the
   first match is often not the one being typed on. */
static void open_keyboards(Plat *p) {
  DIR *d = opendir("/dev/input");
  struct dirent *e;
  if (!d) return;
  while (p->nev < MAX_KBD && (e = readdir(d))) {
    char path[256];
    int fd;
    if (strncmp(e->d_name, "event", 5) != 0) continue;
    snprintf(path, sizeof path, "/dev/input/%s", e->d_name);
    fd = open(path, O_RDONLY | O_NONBLOCK);
    if (fd < 0) continue;
    if (has_letter_keys(fd)) p->ev_fd[p->nev++] = fd;
    else close(fd);
  }
  closedir(d);
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
  if (value == 0 || p->away) return;
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

/* Stop the console from drawing its text and cursor over the editor. */
static void vt_graphics(Plat *p) {
  if (ioctl(STDIN_FILENO, KDSETMODE, KD_GRAPHICS) == 0) p->graphics = MOTE_TRUE;
}

static mote_bool on_vt(void) {
  int mode;
  return ioctl(STDIN_FILENO, KDGETMODE, &mode) == 0;
}

static int vt_number(void) {
  struct stat st;
  if (fstat(STDIN_FILENO, &st) != 0 || major(st.st_rdev) != TTY_MAJOR) return 0;
  return (int)minor(st.st_rdev);
}

/* Evdev sees keys typed on every console, so while the user is on another
   one (say the desktop) input is dropped, and coming back repaints. */
static void vt_check_away(Plat *p) {
  struct vt_stat vs;
  mote_bool away;
  if (!p->vt || ioctl(STDIN_FILENO, VT_GETSTATE, &vs) != 0) return;
  away = vs.v_active != p->vt;
  if (p->away && !away) evq_type(&p->q, PE_EXPOSE);
  p->away = away;
}

Plat *plat_create(const char *title, int w, int h) {
  Plat *p;
  struct fb_var_screeninfo vinfo;
  struct fb_fix_screeninfo finfo;
  const char *dev = getenv("MOTE_FB");
  (void)title;
  /* Under X11 or Wayland the framebuffer is hidden behind the compositor
     and evdev would read keys typed into other windows. */
  if (!dev && !on_vt()) return NULL;
  if (!dev) dev = "/dev/fb0";
  p = (Plat *)calloc(1, sizeof(Plat));
  if (!p) return NULL;
  p->fb_fd = open(dev, O_RDWR);
  if (p->fb_fd < 0) {
    free(p);
    return NULL;
  }
  if (ioctl(p->fb_fd, FBIOGET_VSCREENINFO, &vinfo) < 0 ||
      ioctl(p->fb_fd, FBIOGET_FSCREENINFO, &finfo) < 0 ||
      (vinfo.bits_per_pixel != 32 && vinfo.bits_per_pixel != 16)) {
    plat_destroy(p);
    return NULL;
  }
  p->fb_w = (int)vinfo.xres;
  p->fb_h = (int)vinfo.yres;
  p->fb_bpp = (int)vinfo.bits_per_pixel;
  p->fb_line = (int)finfo.line_length;
  p->fb_r = (int)vinfo.red.offset;
  p->fb_g = (int)vinfo.green.offset;
  p->fb_b = (int)vinfo.blue.offset;
  p->fb_map_sz = (size_t)finfo.smem_len;
  p->fb_map = mmap(NULL, p->fb_map_sz, PROT_READ | PROT_WRITE, MAP_SHARED, p->fb_fd, 0);
  if (p->fb_map == MAP_FAILED) {
    plat_destroy(p);
    return NULL;
  }
  if (w <= 0 || w > p->fb_w) w = p->fb_w;
  if (h <= 0 || h > p->fb_h) h = p->fb_h;
  soft_set_font_px(&p->fb, MOTE_FONT_PX);
  if (!soft_resize(&p->fb, w, h)) {
    plat_destroy(p);
    return NULL;
  }
  p->kx = (int)vinfo.xoffset + (p->fb_w - w) / 2;
  p->ky = (int)vinfo.yoffset + (p->fb_h - h) / 2;
  p->vt = vt_number();
  open_keyboards(p);
  tty_raw(p);
  vt_graphics(p);
  evq_type(&p->q, PE_EXPOSE);
  return p;
}

void plat_destroy(Plat *p) {
  int i;
  if (!p) return;
  if (p->graphics) ioctl(STDIN_FILENO, KDSETMODE, KD_TEXT);
  if (p->raw) tcsetattr(STDIN_FILENO, TCSANOW, &p->saved);
  free(p->clip);
  if (p->fb_map && p->fb_map != MAP_FAILED) munmap(p->fb_map, p->fb_map_sz);
  if (p->fb_fd >= 0) close(p->fb_fd);
  for (i = 0; i < p->nev; i++) close(p->ev_fd[i]);
  soft_free(&p->fb);
  free(p);
}

void plat_wait(Plat *p) {
  struct pollfd pf[MAX_KBD + 1];
  int n = 0, i;
  if (p->q.n > 0) return;
  for (i = 0; i < p->nev; i++) {
    pf[n].fd = p->ev_fd[i];
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
  int i;
  if (evq_pop(&p->q, ev)) return MOTE_TRUE;
  vt_check_away(p);
  for (i = 0; i < p->nev; i++) {
    struct input_event ie;
    while (read(p->ev_fd[i], &ie, sizeof ie) == (ssize_t)sizeof ie) {
      if (ie.type == EV_KEY) map_linux_key(p, ie.code, ie.value);
    }
  }
  /* The VT still queues every key we read from evdev: drain it. Without a
     keyboard device it is the only input, good for Esc and Ctrl+C / Ctrl+Q. */
  if (isatty(STDIN_FILENO)) {
    unsigned char c;
    while (read(STDIN_FILENO, &c, 1) == 1) {
      if (p->nev > 0) continue;
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
  int y, x;
  soft_blit_caret(&p->fb);
  if (!p->fb_map || !p->fb.px || p->away) return;
  for (y = 0; y < p->fb.h; y++) {
    size_t off = (size_t)(p->ky + y) * (size_t)p->fb_line + (size_t)p->kx * (size_t)(p->fb_bpp / 8);
    const mote_u32 *src = p->fb.px + (size_t)y * (size_t)p->fb.w;
    if (off + (size_t)p->fb.w * (size_t)(p->fb_bpp / 8) > p->fb_map_sz) break;
    if (p->fb_bpp == 32 && p->fb_r == 16 && p->fb_g == 8 && p->fb_b == 0) {
      memcpy((char *)p->fb_map + off, src, (size_t)p->fb.w * 4);
    } else if (p->fb_bpp == 32) {
      mote_u32 *dst = (mote_u32 *)((char *)p->fb_map + off);
      for (x = 0; x < p->fb.w; x++) {
        mote_u32 c = src[x];
        dst[x] = ((c >> 16 & 0xFF) << p->fb_r) | ((c >> 8 & 0xFF) << p->fb_g) |
                 ((c & 0xFF) << p->fb_b);
      }
    } else {
      unsigned short *dst = (unsigned short *)((char *)p->fb_map + off);
      for (x = 0; x < p->fb.w; x++) {
        mote_u32 c = src[x];
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
