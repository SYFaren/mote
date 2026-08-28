/* mote overlay/x11 */
#include "platform.h"
#include "common.h"
#include "utf8.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <poll.h>

struct Plat {
  Display *dpy;
  Window win;
  Pixmap back;
  GC gc;
  XFontStruct *font;
  XIM xim;
  XIC xic;
  Atom wm_delete, clipboard, utf8, targets, incr;
  int width, height, depth, font_px;
  long event_mask;
  char *clip_store;
  size_t clip_len;
  /* INCR send to another client */
  Window incr_req;
  Atom incr_prop;
  Atom incr_target;
  size_t incr_off;
  mote_bool incr_active;
};

static void load_font(Plat *p, int px) {
  char pat[128];
  XFontStruct *nf = NULL;
  int tries[] = {0, -1, 1, -2, 2, -3, 3, 4, -4};
  int i;
  if (px < 8) px = 8;
  if (px > 48) px = 48;
  for (i = 0; i < (int)(sizeof tries / sizeof tries[0]); i++) {
    int sz = px + tries[i];
    if (sz < 8) continue;
    snprintf(pat, sizeof pat,
             "-misc-fixed-medium-r-normal--%d-*-*-*-*-*-iso10646-1", sz);
    nf = XLoadQueryFont(p->dpy, pat);
    if (nf) break;
    snprintf(pat, sizeof pat, "-*-*-medium-r-normal--%d-*-*-*-*-*-iso10646-1",
             sz);
    nf = XLoadQueryFont(p->dpy, pat);
    if (nf) break;
  }
  if (!nf) nf = XLoadQueryFont(p->dpy, "9x15");
  if (!nf) nf = XLoadQueryFont(p->dpy, "fixed");
  if (!nf) nf = XLoadQueryFont(p->dpy, "6x13");
  if (!nf) return;
  if (p->font) XFreeFont(p->dpy, p->font);
  p->font = nf;
  p->font_px = px;
  XSetFont(p->dpy, p->gc, p->font->fid);
}

static void ensure_back(Plat *p) {
  int w = p->width > 0 ? p->width : 1;
  int h = p->height > 0 ? p->height : 1;
  if (p->back) {
    XFreePixmap(p->dpy, p->back);
    p->back = None;
  }
  p->back = XCreatePixmap(p->dpy, p->win, (unsigned)w, (unsigned)h,
                          (unsigned)p->depth);
}

static unsigned long scale_chan(unsigned c, unsigned long mask) {
  unsigned long m, shift = 0;
  int bits = 0;
  if (!mask) return 0;
  m = mask;
  while (!(m & 1UL)) {
    m >>= 1;
    shift++;
  }
  while (m & 1UL) {
    bits++;
    m >>= 1;
  }
  if (bits <= 0) return 0;
  return (((unsigned long)c * ((1UL << bits) - 1UL) / 255UL) << shift) & mask;
}

static unsigned long rgb_pixel(Plat *p, mote_u32 rgb) {
  Visual *v = DefaultVisual(p->dpy, DefaultScreen(p->dpy));
  unsigned r = (rgb >> 16) & 0xFF;
  unsigned g = (rgb >> 8) & 0xFF;
  unsigned b = rgb & 0xFF;
  /* TrueColor: no XAllocColor — avoids colormap leak under heavy redraw. */
  if (v && (v->class == TrueColor || v->class == DirectColor)) {
    return scale_chan(r, v->red_mask) | scale_chan(g, v->green_mask) |
           scale_chan(b, v->blue_mask);
  }
  {
    XColor c;
    Colormap cm = DefaultColormap(p->dpy, DefaultScreen(p->dpy));
    c.red = (unsigned short)(r * 257);
    c.green = (unsigned short)(g * 257);
    c.blue = (unsigned short)(b * 257);
    c.flags = DoRed | DoGreen | DoBlue;
    if (!XAllocColor(p->dpy, cm, &c)) return BlackPixel(p->dpy, DefaultScreen(p->dpy));
    return c.pixel;
  }
}

static int x_io_error(Display *d) {
  (void)d;
  /* Don't abort process — main loop will see connection death. */
  return 0;
}

Plat *plat_create(const char *title, int w, int h) {
  Plat *p = (Plat *)calloc(1, sizeof(Plat));
  XSetWindowAttributes swa;
  XGCValues gcv;
  if (!p) return NULL;
  setlocale(LC_CTYPE, "");
  XSetLocaleModifiers("");
  XSetIOErrorHandler(x_io_error);
  p->dpy = XOpenDisplay(NULL);
  if (!p->dpy) { free(p); return NULL; }
  p->width = w;
  p->height = h;
  p->depth = DefaultDepth(p->dpy, DefaultScreen(p->dpy));
  swa.event_mask = ExposureMask | KeyPressMask | ButtonPressMask |
                   ButtonReleaseMask | PointerMotionMask | StructureNotifyMask |
                   FocusChangeMask;
  p->event_mask = swa.event_mask;
  swa.backing_store = Always;
  p->win = XCreateWindow(p->dpy, DefaultRootWindow(p->dpy), 0, 0, (unsigned)w,
                         (unsigned)h, 0, CopyFromParent, InputOutput,
                         CopyFromParent, CWEventMask | CWBackingStore, &swa);
  gcv.graphics_exposures = False;
  p->gc = XCreateGC(p->dpy, p->win, GCGraphicsExposures, &gcv);
  p->font_px = 15;
  load_font(p, 15);
  p->wm_delete = XInternAtom(p->dpy, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(p->dpy, p->win, &p->wm_delete, 1);
  p->clipboard = XInternAtom(p->dpy, "CLIPBOARD", False);
  p->utf8 = XInternAtom(p->dpy, "UTF8_STRING", False);
  p->targets = XInternAtom(p->dpy, "TARGETS", False);
  p->incr = XInternAtom(p->dpy, "INCR", False);
  p->xim = XOpenIM(p->dpy, NULL, NULL, NULL);
  if (p->xim) {
    p->xic = XCreateIC(p->xim, XNInputStyle,
                       XIMPreeditNothing | XIMStatusNothing, XNClientWindow,
                       p->win, XNFocusWindow, p->win, NULL);
  }
  XStoreName(p->dpy, p->win, title);
  ensure_back(p);
  XMapWindow(p->dpy, p->win);
  XFlush(p->dpy);
  return p;
}

void plat_destroy(Plat *p) {
  if (!p) return;
  free(p->clip_store);
  if (p->xic) XDestroyIC(p->xic);
  if (p->xim) XCloseIM(p->xim);
  if (p->back) XFreePixmap(p->dpy, p->back);
  if (p->font) XFreeFont(p->dpy, p->font);
  XFreeGC(p->dpy, p->gc);
  XDestroyWindow(p->dpy, p->win);
  XCloseDisplay(p->dpy);
  free(p);
}

void plat_wait(Plat *p) {
  XEvent e;
  if (!p->dpy) return;
  if (!XPending(p->dpy)) XPeekEvent(p->dpy, &e);
}

void plat_get_size(Plat *p, int *w, int *h) {
  *w = p->width;
  *h = p->height;
}

int plat_font_w(Plat *p) {
  return p->font ? p->font->max_bounds.width : 9;
}

int plat_font_h(Plat *p) {
  return p->font ? p->font->ascent + p->font->descent : 15;
}

void plat_set_font_px(Plat *p, int px) {
  if (!p || !p->dpy) return;
  load_font(p, px);
}

int plat_font_px(Plat *p) { return p && p->font_px > 0 ? p->font_px : 15; }

void plat_begin_frame(Plat *p) { (void)p; }

void plat_clear(Plat *p, mote_u32 rgb) {
  if (!p->back) return;
  XSetForeground(p->dpy, p->gc, rgb_pixel(p, rgb));
  XFillRectangle(p->dpy, p->back, p->gc, 0, 0, (unsigned)p->width,
                 (unsigned)p->height);
}

void plat_fill_rect(Plat *p, int x, int y, int w, int h, mote_u32 rgb) {
  if (!p->back || w <= 0 || h <= 0) return;
  XSetForeground(p->dpy, p->gc, rgb_pixel(p, rgb));
  XFillRectangle(p->dpy, p->back, p->gc, x, y, (unsigned)w, (unsigned)h);
}

void plat_draw_text(Plat *p, int x, int y, const char *s, int n, mote_u32 rgb) {
  int baseline, i = 0, o = 0;
  XChar2b stack[128];
  XChar2b *chars = stack;
  int capa = 128;
  if (!p->back || !s || n <= 0) return;
  baseline = y + (p->font ? p->font->ascent : 12);
  XSetForeground(p->dpy, p->gc, rgb_pixel(p, rgb));
  while (i < n) {
    mote_u32 cp;
    int len = utf8_decode(s + i, (size_t)(n - i), &cp);
    if (len <= 0) {
      i++;
      continue;
    }
    if (o >= capa) {
      int nc = capa * 2;
      XChar2b *nw =
          (XChar2b *)realloc(chars == stack ? NULL : chars, (size_t)nc * sizeof(XChar2b));
      if (!nw) break;
      if (chars == stack) memcpy(nw, stack, (size_t)o * sizeof(XChar2b));
      chars = nw;
      capa = nc;
    }
    if (cp > 0xFFFFu) cp = (mote_u32)'?';
    chars[o].byte1 = (unsigned char)((cp >> 8) & 0xff);
    chars[o].byte2 = (unsigned char)(cp & 0xff);
    o++;
    i += len;
  }
  if (o > 0) XDrawString16(p->dpy, p->back, p->gc, x, baseline, chars, o);
  if (chars != stack) free(chars);
}

void plat_end_frame(Plat *p) {
  if (!p->back) return;
  XCopyArea(p->dpy, p->back, p->win, p->gc, 0, 0, (unsigned)p->width,
            (unsigned)p->height, 0, 0);
  XFlush(p->dpy);
}

void plat_set_title(Plat *p, const char *title) {
  XStoreName(p->dpy, p->win, title);
}

mote_bool plat_set_caret(Plat *p, int x, int y, int h, mote_bool on) {
  (void)p;
  (void)x;
  (void)y;
  (void)h;
  (void)on;
  return MOTE_FALSE; /* software caret */
}

mote_bool plat_clipboard_set(Plat *p, const char *s, size_t n) {
  free(p->clip_store);
  p->clip_store = (char *)malloc(n + 1);
  if (!p->clip_store) return MOTE_FALSE;
  memcpy(p->clip_store, s, n);
  p->clip_store[n] = 0;
  p->clip_len = n;
  p->incr_active = MOTE_FALSE;
  XSetSelectionOwner(p->dpy, p->clipboard, p->win, CurrentTime);
  XSetSelectionOwner(p->dpy, XA_PRIMARY, p->win, CurrentTime);
  return MOTE_TRUE;
}

static size_t clip_quantum(Plat *p) {
  long m = (long)XMaxRequestSize(p->dpy) * 4 - 100;
  if (m > 65536) m = 65536;
  if (m < 4096) m = 4096;
  return (size_t)m;
}

static void incr_send_chunk(Plat *p) {
  size_t q, n;
  if (!p->incr_active || !p->clip_store) return;
  q = clip_quantum(p);
  if (p->incr_off >= p->clip_len) {
    XChangeProperty(p->dpy, p->incr_req, p->incr_prop, p->incr_target, 8,
                    PropModeReplace, (unsigned char *)"", 0);
    p->incr_active = MOTE_FALSE;
    return;
  }
  n = p->clip_len - p->incr_off;
  if (n > q) n = q;
  XChangeProperty(p->dpy, p->incr_req, p->incr_prop, p->incr_target, 8,
                  PropModeReplace,
                  (unsigned char *)(p->clip_store + p->incr_off), (int)n);
  p->incr_off += n;
}

/* ICCCM INCR receive — st/glfw pattern, capped at MOTE_MAX_FILE. */
static char *clip_read_property(Plat *p, Atom prop, size_t *out_len) {
  Atom actual_type;
  int actual_format;
  unsigned long nitems, bytes_after;
  unsigned char *data = NULL;
  char *out = NULL;
  size_t total = 0;

  if (out_len) *out_len = 0;
  if (XGetWindowProperty(p->dpy, p->win, prop, 0, (long)(MOTE_MAX_FILE / 4),
                         False, AnyPropertyType, &actual_type, &actual_format,
                         &nitems, &bytes_after, &data) != Success ||
      !data)
    return NULL;

  if (actual_type == p->incr) {
    int rounds = 0;
    XFree(data);
    XDeleteProperty(p->dpy, p->win, prop);
    p->event_mask |= PropertyChangeMask;
    XSelectInput(p->dpy, p->win, p->event_mask);
    for (;;) {
      XEvent pev;
      int i;
      mote_bool got = MOTE_FALSE;
      size_t chunk;
      struct pollfd pfd;
      if (++rounds > 500) { /* ~10s wall with 20ms polls */
        free(out);
        out = NULL;
        total = 0;
        break;
      }
      for (i = 0; i < 5; i++) {
        if (XCheckTypedWindowEvent(p->dpy, p->win, PropertyNotify, &pev) &&
            pev.xproperty.atom == prop &&
            pev.xproperty.state == PropertyNewValue) {
          got = MOTE_TRUE;
          break;
        }
        XFlush(p->dpy);
        pfd.fd = ConnectionNumber(p->dpy);
        pfd.events = POLLIN;
        if (poll(&pfd, 1, 20) < 0) break;
      }
      if (!got) {
        free(out);
        out = NULL;
        total = 0;
        break;
      }
      if (XGetWindowProperty(p->dpy, p->win, prop, 0,
                             (long)(MOTE_MAX_FILE / 4), True, AnyPropertyType,
                             &actual_type, &actual_format, &nitems,
                             &bytes_after, &data) != Success) {
        free(out);
        out = NULL;
        total = 0;
        break;
      }
      if (!data || nitems == 0) {
        if (data) XFree(data);
        break;
      }
      chunk = nitems;
      if (actual_format == 16) chunk = nitems * 2;
      else if (actual_format == 32) chunk = nitems * 4;
      if (total + chunk > MOTE_MAX_FILE) chunk = MOTE_MAX_FILE - total;
      {
        char *nbuf = (char *)realloc(out, total + chunk + 1);
        if (!nbuf) {
          XFree(data);
          free(out);
          out = NULL;
          total = 0;
          break;
        }
        out = nbuf;
        memcpy(out + total, data, chunk);
        total += chunk;
        out[total] = 0;
      }
      XFree(data);
      if (total >= MOTE_MAX_FILE) break;
    }
    p->event_mask &= ~PropertyChangeMask;
    XSelectInput(p->dpy, p->win, p->event_mask);
    if (out_len) *out_len = total;
    return out;
  }

  {
    size_t nbytes = nitems;
    if (actual_format == 16) nbytes = nitems * 2;
    else if (actual_format == 32) nbytes = nitems * 4;
    if (nbytes > MOTE_MAX_FILE) nbytes = MOTE_MAX_FILE;
    out = (char *)malloc(nbytes + 1);
    if (out) {
      memcpy(out, data, nbytes);
      out[nbytes] = 0;
      if (out_len) *out_len = nbytes;
    }
  }
  XFree(data);
  return out;
}

static int x_wait_event(Plat *p, int type, XEvent *ev, int slices) {
  int i;
  for (i = 0; i < slices; i++) {
    struct pollfd pfd;
    if (XCheckTypedWindowEvent(p->dpy, p->win, type, ev)) return 1;
    XFlush(p->dpy);
    pfd.fd = ConnectionNumber(p->dpy);
    pfd.events = POLLIN;
    if (poll(&pfd, 1, 20) < 0) break;
  }
  return 0;
}

char *plat_clipboard_get(Plat *p, size_t *out_len) {
  char *out;
  XEvent ev;
  if (out_len) *out_len = 0;
  if (XGetSelectionOwner(p->dpy, p->clipboard) == p->win && p->clip_store) {
    out = (char *)malloc(p->clip_len + 1);
    if (!out) return NULL;
    memcpy(out, p->clip_store, p->clip_len + 1);
    if (out_len) *out_len = p->clip_len;
    return out;
  }
  memset(&ev, 0, sizeof ev);
  XConvertSelection(p->dpy, p->clipboard, p->utf8, p->clipboard, p->win,
                    CurrentTime);
  x_wait_event(p, SelectionNotify, &ev, 50);
  if (ev.type != SelectionNotify || ev.xselection.property == None) {
    XConvertSelection(p->dpy, p->clipboard, XA_STRING, p->clipboard, p->win,
                      CurrentTime);
    x_wait_event(p, SelectionNotify, &ev, 50);
  }
  if (ev.type != SelectionNotify || ev.xselection.property == None) {
    if (!p->clip_store) return NULL;
    out = (char *)malloc(p->clip_len + 1);
    if (!out) return NULL;
    memcpy(out, p->clip_store, p->clip_len + 1);
    if (out_len) *out_len = p->clip_len;
    return out;
  }
  return clip_read_property(p, p->clipboard, out_len);
}

static void map_key(KeySym ks, unsigned state, PlatEvent *ev) {
  mote_bool ctrl = (state & ControlMask) != 0;
  mote_bool shift = (state & ShiftMask) != 0;
  mote_bool alt = (state & Mod1Mask) != 0;
  KeySym lower = ks;
  if (ks >= XK_A && ks <= XK_Z) lower = ks + 32;
  ev->type = PE_KEY;
  ev->ctrl = ctrl;
  ev->shift = shift;
  ev->key = PK_NONE;
  if (alt && !ctrl) {
    if (lower == XK_c) { ev->key = PK_FINDCASE; return; }
    if (lower == XK_w) { ev->key = PK_FINDWORD; return; }
    if (lower == XK_s) { ev->key = PK_SAVEAS; return; }
    if (lower == XK_r) { ev->key = PK_READONLY; return; }
    if (lower == XK_k) { ev->key = PK_DELLINE; return; }
    if (lower == XK_e) { ev->key = PK_EOL; return; }
    if (lower == XK_h) { ev->key = PK_HELP; return; }
    if (lower == XK_n) { ev->key = PK_NEXTDOC; return; }
    if (lower == XK_p) { ev->key = PK_PREVDOC; return; }
    if (lower == XK_m) { ev->key = PK_BOOKMARK_SET; return; }
    if (lower == XK_j) { ev->key = PK_BOOKMARK; return; }
  }
  if (ctrl) {
    switch (lower) {
    case XK_s: ev->key = shift ? PK_SAVEAS : PK_SAVE; return;
    case XK_o: ev->key = PK_OPEN; return;
    case XK_q: ev->key = PK_QUIT; return;
    case XK_z: ev->key = PK_UNDO; return;
    case XK_y: ev->key = PK_REDO; return;
    case XK_f: ev->key = PK_FIND; return;
    case XK_x: ev->key = PK_CUT; return;
    case XK_c: ev->key = PK_COPY; return;
    case XK_v: ev->key = PK_PASTE; return;
    case XK_a: ev->key = PK_SELALL; return;
    case XK_h: ev->key = PK_HELP; return;
    case XK_t: ev->key = PK_THEME; return;
    case XK_g: ev->key = PK_GOTO; return;
    case XK_r: ev->key = shift ? PK_READONLY : PK_REPLACE; return;
    case XK_w: ev->key = shift ? PK_CLOSEDOC : PK_WRAP; return;
    case XK_d: ev->key = PK_DUPLINE; return;
    case XK_k: if (shift) { ev->key = PK_DELLINE; return; } break;
    case XK_n: ev->key = PK_NEWDOC; return;
    case XK_m:
      if (shift) {
        ev->key = PK_BOOKMARK_SET;
        return;
      }
      ev->key = PK_BOOKMARK;
      return;
    case XK_j:
      ev->key = PK_BOOKMARK;
      return;
    case XK_p: ev->key = shift ? PK_BOOKMARK_SET : PK_QUICKOPEN; return;
    case XK_e: ev->key = shift ? PK_EOL : PK_RECENT; return;
    case XK_slash:
    case XK_question: ev->key = PK_COMMENT; return;
    case XK_bracketright: ev->key = PK_BRACKET; return;
    case XK_backslash: if (shift) { ev->key = PK_BRACKET; return; } break;
    case XK_equal:
    case XK_plus:
    case XK_KP_Add: ev->key = PK_ZOOMIN; return;
    case XK_minus:
    case XK_KP_Subtract: ev->key = PK_ZOOMOUT; return;
    case XK_0:
    case XK_KP_0: ev->key = PK_ZOOMRESET; return;
    case XK_Tab:
    case XK_ISO_Left_Tab:
      ev->key = shift ? PK_PREVDOC : PK_NEXTDOC;
      return;
    default: break;
    }
  }
  switch (ks) {
  case XK_Left: ev->key = PK_LEFT; break;
  case XK_Right: ev->key = PK_RIGHT; break;
  case XK_Up: ev->key = PK_UP; break;
  case XK_Down: ev->key = PK_DOWN; break;
  case XK_Home: ev->key = PK_HOME; break;
  case XK_End: ev->key = PK_END; break;
  case XK_Page_Up: ev->key = PK_PGUP; break;
  case XK_Page_Down: ev->key = PK_PGDN; break;
  case XK_BackSpace: ev->key = PK_BACKSPACE; break;
  case XK_Delete: ev->key = PK_DELETE; break;
  case XK_Return:
  case XK_KP_Enter:
    if (ctrl && shift) {
      ev->key = PK_BOOKMARK_SET;
      return;
    }
    if (ctrl) {
      ev->key = PK_BOOKMARK;
      return;
    }
    ev->key = PK_ENTER;
    break;
  case XK_Escape: ev->key = PK_ESCAPE; break;
  case XK_Tab:
  case XK_ISO_Left_Tab: ev->key = PK_TAB; break;
  case XK_F1: ev->key = PK_F1; break;
  case XK_F3: ev->key = shift ? PK_FINDPREV : PK_FINDNEXT; break;
  case XK_F5: ev->key = PK_RELOAD; break;
  case XK_F7: ev->key = PK_WS; break;
  default: ev->type = PE_NONE; break;
  }
}

mote_bool plat_poll(Plat *p, PlatEvent *ev) {
  XEvent xev;
  memset(ev, 0, sizeof *ev);
  while (XPending(p->dpy)) {
    XNextEvent(p->dpy, &xev);
    switch (xev.type) {
    case ClientMessage:
      if ((Atom)xev.xclient.data.l[0] == p->wm_delete) {
        ev->type = PE_QUIT;
        return MOTE_TRUE;
      }
      break;
    case ConfigureNotify: {
      XEvent more;
      while (XCheckTypedWindowEvent(p->dpy, p->win, ConfigureNotify, &more))
        xev = more;
      if (xev.xconfigure.width != p->width ||
          xev.xconfigure.height != p->height) {
        int nw = xev.xconfigure.width;
        int nh = xev.xconfigure.height;
        if (nw < 1) nw = 1;
        if (nh < 1) nh = 1;
        if (nw > 16384) nw = 16384;
        if (nh > 16384) nh = 16384;
        p->width = nw;
        p->height = nh;
        ensure_back(p);
        ev->type = PE_EXPOSE;
        return MOTE_TRUE;
      }
      break;
    }
    case FocusIn:
      if (p->xic) XSetICFocus(p->xic);
      break;
    case FocusOut:
      if (p->xic) XUnsetICFocus(p->xic);
      break;
    case Expose:
      if (xev.xexpose.count == 0) {
        ev->type = PE_EXPOSE;
        return MOTE_TRUE;
      }
      break;
    case KeyPress: {
      KeySym ks = NoSymbol;
      char buf[64];
      int n = 0;
      Status st = 0;
      if (XFilterEvent(&xev, p->win)) break;
#ifdef X_HAVE_UTF8_STRING
      if (p->xic)
        n = Xutf8LookupString(p->xic, &xev.xkey, buf, (int)sizeof buf, &ks,
                              &st);
      else
#endif
        n = XLookupString(&xev.xkey, buf, (int)sizeof buf, &ks, NULL);
      if (n < 0) n = 0;
      map_key(ks, xev.xkey.state, ev);
      if (ev->type == PE_KEY) return MOTE_TRUE;
      if (n > 0 && !(xev.xkey.state & ControlMask) &&
          (unsigned char)buf[0] >= 32) {
        ev->type = PE_TEXT;
        if (n > 31) n = 31;
        memcpy(ev->text, buf, (size_t)n);
        ev->text_len = n;
        ev->text[n] = 0;
        return MOTE_TRUE;
      }
      break;
    }
    case ButtonPress:
      if (xev.xbutton.button == Button4 || xev.xbutton.button == Button5) {
        ev->type = PE_SCROLL;
        ev->wheel = xev.xbutton.button == Button4 ? 3 : -3;
        return MOTE_TRUE;
      }
      if (xev.xbutton.button == Button1) {
        ev->type = PE_MOUSE_DOWN;
        ev->mx = xev.xbutton.x;
        ev->my = xev.xbutton.y;
        ev->shift = (xev.xbutton.state & ShiftMask) != 0;
        return MOTE_TRUE;
      }
      break;
    case ButtonRelease:
      if (xev.xbutton.button == Button1) {
        ev->type = PE_MOUSE_UP;
        ev->mx = xev.xbutton.x;
        ev->my = xev.xbutton.y;
        return MOTE_TRUE;
      }
      break;
    case MotionNotify:
      if (xev.xmotion.state & Button1Mask) {
        while (XCheckTypedWindowEvent(p->dpy, p->win, MotionNotify, &xev)) {
        }
        ev->type = PE_MOUSE_MOVE;
        ev->mx = xev.xmotion.x;
        ev->my = xev.xmotion.y;
        return MOTE_TRUE;
      }
      break;
    case SelectionClear:
      p->incr_active = MOTE_FALSE;
      break;
    case PropertyNotify:
      if (p->incr_active && xev.xproperty.window == p->incr_req &&
          xev.xproperty.atom == p->incr_prop &&
          xev.xproperty.state == PropertyDelete)
        incr_send_chunk(p);
      break;
    case SelectionRequest: {
      XSelectionRequestEvent *req = &xev.xselectionrequest;
      XSelectionEvent sev;
      size_t q = clip_quantum(p);
      memset(&sev, 0, sizeof sev);
      sev.type = SelectionNotify;
      sev.display = req->display;
      sev.requestor = req->requestor;
      sev.selection = req->selection;
      sev.target = req->target;
      sev.time = req->time;
      sev.property = None;
      if (req->property == None) req->property = req->target;
      if (p->clip_store &&
          (req->target == XA_STRING || req->target == p->utf8)) {
        if (p->incr_active) {
          /* refuse overlapping INCR — keep current transfer intact */
        } else if (p->clip_len > q) {
          long sz = (long)p->clip_len;
          p->incr_active = MOTE_TRUE;
          p->incr_req = req->requestor;
          p->incr_prop = req->property;
          p->incr_target = req->target;
          p->incr_off = 0;
          XSelectInput(p->dpy, req->requestor, PropertyChangeMask);
          XChangeProperty(p->dpy, req->requestor, req->property, p->incr, 32,
                          PropModeReplace, (unsigned char *)&sz, 1);
          sev.property = req->property;
        } else {
          XChangeProperty(p->dpy, req->requestor, req->property, req->target,
                          8, PropModeReplace, (unsigned char *)p->clip_store,
                          (int)p->clip_len);
          sev.property = req->property;
        }
      } else if (req->target == p->targets) {
        Atom al[2] = {p->utf8, XA_STRING};
        XChangeProperty(p->dpy, req->requestor, req->property, XA_ATOM, 32,
                        PropModeReplace, (unsigned char *)al, 2);
        sev.property = req->property;
      }
      XSendEvent(p->dpy, req->requestor, False, 0, (XEvent *)&sev);
      break;
    }
    default:
      break;
    }
  }
  return MOTE_FALSE;
}
