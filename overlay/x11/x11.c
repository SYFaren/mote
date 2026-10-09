#include "platform.h"
#include "keymap.h"
#include "common.h"
#include "soft.h"

#include <X11/Xlib.h>
#include <X11/Xutil.h>
#include <X11/keysym.h>
#include <X11/Xatom.h>

#include <locale.h>
#include <stdlib.h>
#include <string.h>
#include <stdio.h>
#include <poll.h>

/* Text goes through the soft renderer and its built-in font: core X fonts
   are often Latin-1 only (Xwayland ships just "fixed") and come in few
   sizes. */
struct Plat {
  Display *dpy;
  Window win;
  GC gc;
  SoftFb fb;
  XImage *img;
  mote_bool img_shared; /* img->data is fb.px */
  XIM xim;
  XIC xic;
  Atom wm_delete, wm_name, clipboard, utf8, targets, incr;
  int width, height, depth;
  long event_mask;
  char *clip_store;
  size_t clip_len;
  /* INCR transfer to another client */
  Window incr_req;
  Atom incr_prop;
  Atom incr_target;
  size_t incr_off;
  mote_bool incr_active;
};

static void free_image(Plat *p) {
  if (!p->img) return;
  if (p->img_shared) p->img->data = NULL; /* fb.px is not XDestroyImage's to free */
  XDestroyImage(p->img);
  p->img = NULL;
}

/* Framebuffer and XImage at the window size. 0x00RRGGBB pixels go out as
   they are on the usual 24/32-bit visual; other visuals get converted. */
static mote_bool ensure_image(Plat *p) {
  Visual *v = DefaultVisual(p->dpy, DefaultScreen(p->dpy));
  unsigned one = 1;
  free_image(p);
  if (!soft_resize(&p->fb, p->width, p->height)) return MOTE_FALSE;
  p->img = XCreateImage(p->dpy, v, (unsigned)p->depth, ZPixmap, 0, NULL,
                        (unsigned)p->width, (unsigned)p->height, 32, 0);
  if (!p->img) return MOTE_FALSE;
  p->img_shared = p->img->bits_per_pixel == 32 && v->red_mask == 0xFF0000UL &&
                  v->green_mask == 0xFF00UL && v->blue_mask == 0xFFUL;
  if (p->img_shared) {
    p->img->data = (char *)p->fb.px;
    p->img->byte_order = *(unsigned char *)&one ? LSBFirst : MSBFirst;
    return MOTE_TRUE;
  }
  p->img->data = (char *)malloc((size_t)p->img->bytes_per_line * (size_t)p->height);
  if (!p->img->data) {
    free_image(p);
    return MOTE_FALSE;
  }
  return MOTE_TRUE;
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

static void convert_image(Plat *p) {
  Visual *v = DefaultVisual(p->dpy, DefaultScreen(p->dpy));
  int x, y;
  for (y = 0; y < p->fb.h; y++)
    for (x = 0; x < p->fb.w; x++) {
      mote_u32 c = p->fb.px[(size_t)y * (size_t)p->fb.w + (size_t)x];
      XPutPixel(p->img, x, y,
                scale_chan((c >> 16) & 0xFF, v->red_mask) |
                    scale_chan((c >> 8) & 0xFF, v->green_mask) |
                    scale_chan(c & 0xFF, v->blue_mask));
    }
}

static int x_io_error(Display *d) {
  (void)d;
  /* Don't exit here; the main loop notices the lost connection. */
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
  soft_set_font_px(&p->fb, MOTE_FONT_PX);
  p->wm_delete = XInternAtom(p->dpy, "WM_DELETE_WINDOW", False);
  XSetWMProtocols(p->dpy, p->win, &p->wm_delete, 1);
  p->wm_name = XInternAtom(p->dpy, "_NET_WM_NAME", False);
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
  plat_set_title(p, title);
  if (!ensure_image(p)) {
    plat_destroy(p);
    return NULL;
  }
  XMapWindow(p->dpy, p->win);
  XFlush(p->dpy);
  return p;
}

void plat_destroy(Plat *p) {
  if (!p) return;
  free(p->clip_store);
  if (p->xic) XDestroyIC(p->xic);
  if (p->xim) XCloseIM(p->xim);
  free_image(p);
  soft_free(&p->fb);
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
  if (!p->img) return;
  if (!p->img_shared) convert_image(p);
  XPutImage(p->dpy, p->win, p->gc, p->img, 0, 0, 0, 0, (unsigned)p->width,
            (unsigned)p->height);
  XFlush(p->dpy);
}

void plat_set_title(Plat *p, const char *title) {
  XStoreName(p->dpy, p->win, title);
  XChangeProperty(p->dpy, p->win, p->wm_name, p->utf8, 8, PropModeReplace,
                  (const unsigned char *)title, (int)strlen(title));
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

/* Receive an ICCCM INCR transfer, up to MOTE_MAX_FILE. */
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
      if (++rounds > 500) { /* about 10 s of 20 ms polls */
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

/* ASCII for a keysym as the shared keymap sees it (keypad folded in). */
static int ks_char(KeySym ks) {
  switch (ks) {
  case XK_KP_Add: return '+';
  case XK_KP_Subtract: return '-';
  case XK_KP_0: return '0';
  case XK_Tab:
  case XK_ISO_Left_Tab: return '\t';
  case XK_Return:
  case XK_KP_Enter: return '\r';
  default: return ks >= 0x20 && ks < 0x7f ? (int)ks : 0;
  }
}

static void map_key(XKeyEvent *xk, KeySym ks, PlatEvent *ev) {
  mote_bool ctrl = (xk->state & ControlMask) != 0;
  mote_bool shift = (xk->state & ShiftMask) != 0;
  mote_bool alt = (xk->state & Mod1Mask) != 0;
  int ch = ks_char(ks), i;
  /* Non-Latin layout: shortcuts use the key's Latin symbol from whichever
     group has one; the Latin layout need not be the first. */
  for (i = 0; !ch && (ctrl || alt) && i < 8; i += 2) ch = ks_char(XLookupKeysym(xk, i));
  ev->type = PE_KEY;
  ev->ctrl = ctrl;
  ev->shift = shift;
  if (alt && !ctrl && ch) ev->key = key_alt(ch);
  else if (ctrl && ch) ev->key = key_ctrl(ch, shift);
  else ev->key = PK_NONE;
  if (ev->key != PK_NONE) return;
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
  case XK_KP_Enter: ev->key = PK_ENTER; break;
  case XK_Escape: ev->key = PK_ESCAPE; break;
  case XK_Tab:
  case XK_ISO_Left_Tab: ev->key = PK_TAB; break;
  default:
    if (ks >= XK_F1 && ks <= XK_F12) ev->key = key_fn((int)(ks - XK_F1) + 1, ctrl, shift);
    break;
  }
  if (ev->key == PK_NONE) ev->type = PE_NONE;
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
        ensure_image(p);
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
      map_key(&xev.xkey, ks, ev);
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
          /* one INCR transfer at a time */
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
