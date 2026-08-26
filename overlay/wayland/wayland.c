/* mote overlay/wayland — wl_shm + xdg-shell + xkbcommon */
#include "platform.h"
#include "soft.h"
#include "soft_keys.h"

#include "xdg-shell-client-protocol.h"

#include <wayland-client.h>
#include <xkbcommon/xkbcommon.h>

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

struct WlBuf {
  struct wl_buffer *buf;
  void *data;
  size_t size;
  int fd;
  int busy;
};

struct Plat {
  SoftFb fb;
  struct wl_display *dpy;
  struct wl_registry *reg;
  struct wl_compositor *comp;
  struct wl_shm *shm;
  struct xdg_wm_base *xdg;
  struct wl_seat *seat;
  struct wl_surface *surf;
  struct xdg_surface *xdgs;
  struct xdg_toplevel *top;
  struct wl_keyboard *kb;
  struct wl_pointer *ptr;
  struct xkb_context *xkb_ctx;
  struct xkb_keymap *xkb_map;
  struct xkb_state *xkb_state;
  struct WlBuf slot[2];
  int slot_w, slot_h;
  int configured;
  int running;
  int mx, my;
  char *clip;
  size_t clip_n;
  PlatEvent q[128];
  int qn;
  mote_bool ctrl, shift, alt;
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

static int anon_shm(size_t size) {
  int fd = memfd_create("mote-wl", 0);
  if (fd < 0) {
    char path[] = "/tmp/mote-wl-XXXXXX";
    fd = mkstemp(path);
    if (fd >= 0) unlink(path);
  }
  if (fd < 0) return -1;
  if (ftruncate(fd, (off_t)size) < 0) {
    close(fd);
    return -1;
  }
  return fd;
}

static void buffer_release(void *data, struct wl_buffer *buf) {
  struct WlBuf *s = (struct WlBuf *)data;
  (void)buf;
  s->busy = 0;
}
static const struct wl_buffer_listener buffer_listener = {buffer_release};

static void destroy_slot(struct WlBuf *s) {
  if (s->buf) {
    wl_buffer_destroy(s->buf);
    s->buf = NULL;
  }
  if (s->data && s->data != MAP_FAILED) {
    munmap(s->data, s->size);
    s->data = NULL;
  }
  if (s->fd >= 0) {
    close(s->fd);
    s->fd = -1;
  }
  s->size = 0;
  s->busy = 0;
}

static void destroy_bufs(Plat *p) {
  destroy_slot(&p->slot[0]);
  destroy_slot(&p->slot[1]);
  p->slot_w = p->slot_h = 0;
}

static mote_bool make_slot(Plat *p, struct WlBuf *s, int w, int h) {
  struct wl_shm_pool *pool;
  int stride = w * 4;
  size_t sz = (size_t)stride * (size_t)h;
  destroy_slot(s);
  s->fd = anon_shm(sz);
  if (s->fd < 0) return MOTE_FALSE;
  s->data = mmap(NULL, sz, PROT_READ | PROT_WRITE, MAP_SHARED, s->fd, 0);
  if (s->data == MAP_FAILED) {
    destroy_slot(s);
    return MOTE_FALSE;
  }
  s->size = sz;
  pool = wl_shm_create_pool(p->shm, s->fd, (int32_t)sz);
  s->buf = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_XRGB8888);
  wl_shm_pool_destroy(pool);
  if (!s->buf) {
    destroy_slot(s);
    return MOTE_FALSE;
  }
  wl_buffer_add_listener(s->buf, &buffer_listener, s);
  s->busy = 0;
  return MOTE_TRUE;
}

static mote_bool make_bufs(Plat *p) {
  int w = p->fb.w, h = p->fb.h;
  if (w < 1 || h < 1) return MOTE_FALSE;
  if (p->slot_w == w && p->slot_h == h && p->slot[0].buf && p->slot[1].buf)
    return MOTE_TRUE;
  destroy_bufs(p);
  if (!make_slot(p, &p->slot[0], w, h) || !make_slot(p, &p->slot[1], w, h)) {
    destroy_bufs(p);
    return MOTE_FALSE;
  }
  p->slot_w = w;
  p->slot_h = h;
  return MOTE_TRUE;
}

static struct WlBuf *free_slot(Plat *p) {
  int i;
  for (i = 0; i < 2; i++)
    if (p->slot[i].buf && !p->slot[i].busy) return &p->slot[i];
  return NULL;
}

static void xdg_wm_base_ping(void *data, struct xdg_wm_base *xdg, uint32_t serial) {
  (void)data;
  xdg_wm_base_pong(xdg, serial);
}
static const struct xdg_wm_base_listener xdg_wm_base_listener = {
    xdg_wm_base_ping};

static void xdg_surface_configure(void *data, struct xdg_surface *xs, uint32_t serial) {
  Plat *p = (Plat *)data;
  xdg_surface_ack_configure(xs, serial);
  p->configured = 1;
}
static const struct xdg_surface_listener xdg_surface_listener = {
    xdg_surface_configure};

static void xdg_toplevel_configure(void *data, struct xdg_toplevel *top,
                                   int32_t w, int32_t h, struct wl_array *states) {
  Plat *p = (Plat *)data;
  PlatEvent e;
  (void)top;
  (void)states;
  /* Compositor may pass 0,0 = "client chooses". Keep current size then. */
  if (w > 0 && h > 0) {
    if (w < 200) w = 200;
    if (h < 120) h = 120;
    if (w != p->fb.w || h != p->fb.h) {
      soft_resize(&p->fb, w, h);
      make_bufs(p);
    }
    if (p->xdgs)
      xdg_surface_set_window_geometry(p->xdgs, 0, 0, p->fb.w, p->fb.h);
  }
  memset(&e, 0, sizeof e);
  e.type = PE_EXPOSE;
  qpush(p, &e);
}
static void xdg_toplevel_close(void *data, struct xdg_toplevel *top) {
  Plat *p = (Plat *)data;
  PlatEvent e;
  (void)top;
  p->running = 0;
  memset(&e, 0, sizeof e);
  e.type = PE_QUIT;
  qpush(p, &e);
}
static void xdg_toplevel_configure_bounds(void *d, struct xdg_toplevel *t, int32_t w,
                                          int32_t h) {
  (void)d;
  (void)t;
  (void)w;
  (void)h;
}
static void xdg_toplevel_wm_capabilities(void *d, struct xdg_toplevel *t,
                                         struct wl_array *caps) {
  (void)d;
  (void)t;
  (void)caps;
}
static const struct xdg_toplevel_listener xdg_toplevel_listener = {
    xdg_toplevel_configure, xdg_toplevel_close, xdg_toplevel_configure_bounds,
    xdg_toplevel_wm_capabilities};

static void kb_keymap(void *data, struct wl_keyboard *kb, uint32_t format, int fd,
                      uint32_t size) {
  Plat *p = (Plat *)data;
  char *map_shm;
  (void)kb;
  if (format != WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1) {
    close(fd);
    return;
  }
  map_shm = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
  close(fd);
  if (map_shm == MAP_FAILED) return;
  if (p->xkb_map) xkb_keymap_unref(p->xkb_map);
  if (p->xkb_state) xkb_state_unref(p->xkb_state);
  p->xkb_map = xkb_keymap_new_from_string(p->xkb_ctx, map_shm,
                                          XKB_KEYMAP_FORMAT_TEXT_V1,
                                          XKB_KEYMAP_COMPILE_NO_FLAGS);
  munmap(map_shm, size);
  if (!p->xkb_map) return;
  p->xkb_state = xkb_state_new(p->xkb_map);
}

static void kb_enter(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf,
                     struct wl_array *keys) {
  (void)d;
  (void)k;
  (void)s;
  (void)sf;
  (void)keys;
}
static void kb_leave(void *d, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf) {
  (void)d;
  (void)k;
  (void)s;
  (void)sf;
}

static void kb_key(void *data, struct wl_keyboard *kb, uint32_t serial, uint32_t time,
                   uint32_t key, uint32_t state) {
  Plat *p = (Plat *)data;
  xkb_keysym_t sym;
  char buf[32];
  int n;
  (void)kb;
  (void)serial;
  (void)time;
  if (!p->xkb_state || state != WL_KEYBOARD_KEY_STATE_PRESSED) return;
  sym = xkb_state_key_get_one_sym(p->xkb_state, key + 8);
  p->ctrl = xkb_state_mod_name_is_active(p->xkb_state, XKB_MOD_NAME_CTRL,
                                         XKB_STATE_MODS_EFFECTIVE) > 0;
  p->shift = xkb_state_mod_name_is_active(p->xkb_state, XKB_MOD_NAME_SHIFT,
                                          XKB_STATE_MODS_EFFECTIVE) > 0;
  p->alt = xkb_state_mod_name_is_active(p->xkb_state, XKB_MOD_NAME_ALT,
                                        XKB_STATE_MODS_EFFECTIVE) > 0;

  if (p->ctrl || p->alt) {
    PlatKey pk = PK_NONE;
    if (sym >= XKB_KEY_a && sym <= XKB_KEY_z)
      pk = soft_ctrl_letter((int)('A' + (sym - XKB_KEY_a)), p->shift, p->alt);
    else if (sym >= XKB_KEY_A && sym <= XKB_KEY_Z)
      pk = soft_ctrl_letter((int)sym, p->shift, p->alt);
    else if (sym == XKB_KEY_equal || sym == XKB_KEY_plus)
      pk = PK_ZOOMIN;
    else if (sym == XKB_KEY_minus)
      pk = PK_ZOOMOUT;
    else if (sym == XKB_KEY_0)
      pk = PK_ZOOMRESET;
    else if (sym == XKB_KEY_bracketright)
      pk = PK_BRACKET;
    else if (sym == XKB_KEY_Tab)
      pk = p->shift ? PK_PREVDOC : PK_NEXTDOC;
    if (pk != PK_NONE) {
      key_nav(p, pk);
      return;
    }
  }

  switch (sym) {
  case XKB_KEY_Left: key_nav(p, PK_LEFT); return;
  case XKB_KEY_Right: key_nav(p, PK_RIGHT); return;
  case XKB_KEY_Up: key_nav(p, PK_UP); return;
  case XKB_KEY_Down: key_nav(p, PK_DOWN); return;
  case XKB_KEY_Home: key_nav(p, PK_HOME); return;
  case XKB_KEY_End: key_nav(p, PK_END); return;
  case XKB_KEY_Page_Up: key_nav(p, PK_PGUP); return;
  case XKB_KEY_Page_Down: key_nav(p, PK_PGDN); return;
  case XKB_KEY_BackSpace: key_nav(p, PK_BACKSPACE); return;
  case XKB_KEY_Delete: key_nav(p, PK_DELETE); return;
  case XKB_KEY_Return:
  case XKB_KEY_KP_Enter: key_nav(p, PK_ENTER); return;
  case XKB_KEY_Escape: key_nav(p, PK_ESCAPE); return;
  case XKB_KEY_Tab: key_nav(p, PK_TAB); return;
  case XKB_KEY_F1: key_nav(p, PK_F1); return;
  case XKB_KEY_F3: key_nav(p, p->shift ? PK_FINDPREV : PK_FINDNEXT); return;
  case XKB_KEY_F4:
    if (p->ctrl) key_nav(p, PK_CLOSEDOC);
    return;
  case XKB_KEY_F5: key_nav(p, PK_RELOAD); return;
  case XKB_KEY_F7: key_nav(p, PK_WS); return;
  default: break;
  }

  if (p->ctrl || p->alt) return;
  n = xkb_state_key_get_utf8(p->xkb_state, key + 8, buf, sizeof buf);
  if (n > 0) {
    PlatEvent e;
    memset(&e, 0, sizeof e);
    e.type = PE_TEXT;
    if (n > (int)sizeof e.text) n = (int)sizeof e.text;
    memcpy(e.text, buf, (size_t)n);
    e.text_len = n;
    qpush(p, &e);
  }
}

static void kb_modifiers(void *data, struct wl_keyboard *kb, uint32_t serial,
                         uint32_t depressed, uint32_t latched, uint32_t locked,
                         uint32_t group) {
  Plat *p = (Plat *)data;
  (void)kb;
  (void)serial;
  if (p->xkb_state)
    xkb_state_update_mask(p->xkb_state, depressed, latched, locked, 0, 0, group);
}
static void kb_repeat(void *d, struct wl_keyboard *k, int32_t r, int32_t delay) {
  (void)d;
  (void)k;
  (void)r;
  (void)delay;
}
static const struct wl_keyboard_listener kb_listener = {
    kb_keymap, kb_enter, kb_leave, kb_key, kb_modifiers, kb_repeat};

static void ptr_enter(void *data, struct wl_pointer *ptr, uint32_t serial,
                      struct wl_surface *surf, wl_fixed_t sx, wl_fixed_t sy) {
  Plat *p = (Plat *)data;
  (void)ptr;
  (void)serial;
  (void)surf;
  p->mx = wl_fixed_to_int(sx);
  p->my = wl_fixed_to_int(sy);
}
static void ptr_leave(void *d, struct wl_pointer *p, uint32_t s, struct wl_surface *sf) {
  (void)d;
  (void)p;
  (void)s;
  (void)sf;
}
static void ptr_motion(void *data, struct wl_pointer *ptr, uint32_t time, wl_fixed_t sx,
                       wl_fixed_t sy) {
  Plat *p = (Plat *)data;
  PlatEvent e;
  (void)ptr;
  (void)time;
  p->mx = wl_fixed_to_int(sx);
  p->my = wl_fixed_to_int(sy);
  memset(&e, 0, sizeof e);
  e.type = PE_MOUSE_MOVE;
  e.mx = p->mx;
  e.my = p->my;
  qpush(p, &e);
}
static void ptr_button(void *data, struct wl_pointer *ptr, uint32_t serial, uint32_t time,
                       uint32_t button, uint32_t state) {
  Plat *p = (Plat *)data;
  PlatEvent e;
  (void)ptr;
  (void)serial;
  (void)time;
  (void)button;
  memset(&e, 0, sizeof e);
  e.type = state == WL_POINTER_BUTTON_STATE_PRESSED ? PE_MOUSE_DOWN : PE_MOUSE_UP;
  e.mx = p->mx;
  e.my = p->my;
  qpush(p, &e);
}
static void ptr_axis(void *data, struct wl_pointer *ptr, uint32_t time, uint32_t axis,
                     wl_fixed_t value) {
  Plat *p = (Plat *)data;
  PlatEvent e;
  (void)ptr;
  (void)time;
  if (axis != WL_POINTER_AXIS_VERTICAL_SCROLL) return;
  memset(&e, 0, sizeof e);
  e.type = PE_SCROLL;
  e.wheel = wl_fixed_to_int(value) > 0 ? -1 : 1;
  qpush(p, &e);
}
static void ptr_frame(void *d, struct wl_pointer *p) {
  (void)d;
  (void)p;
}
static void ptr_axis_source(void *d, struct wl_pointer *p, uint32_t s) {
  (void)d;
  (void)p;
  (void)s;
}
static void ptr_axis_stop(void *d, struct wl_pointer *p, uint32_t t, uint32_t a) {
  (void)d;
  (void)p;
  (void)t;
  (void)a;
}
static void ptr_axis_discrete(void *d, struct wl_pointer *p, uint32_t a, int32_t disc) {
  (void)d;
  (void)p;
  (void)a;
  (void)disc;
}
static void ptr_axis_value120(void *d, struct wl_pointer *p, uint32_t a, int32_t v) {
  (void)d;
  (void)p;
  (void)a;
  (void)v;
}
static const struct wl_pointer_listener ptr_listener = {
    ptr_enter,
    ptr_leave,
    ptr_motion,
    ptr_button,
    ptr_axis,
    ptr_frame,
    ptr_axis_source,
    ptr_axis_stop,
    ptr_axis_discrete,
    ptr_axis_value120};

static void seat_caps(void *data, struct wl_seat *seat, uint32_t caps) {
  Plat *p = (Plat *)data;
  if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !p->kb) {
    p->kb = wl_seat_get_keyboard(seat);
    wl_keyboard_add_listener(p->kb, &kb_listener, p);
  }
  if ((caps & WL_SEAT_CAPABILITY_POINTER) && !p->ptr) {
    p->ptr = wl_seat_get_pointer(seat);
    wl_pointer_add_listener(p->ptr, &ptr_listener, p);
  }
}
static void seat_name(void *d, struct wl_seat *s, const char *n) {
  (void)d;
  (void)s;
  (void)n;
}
static const struct wl_seat_listener seat_listener = {seat_caps, seat_name};

static void registry_global(void *data, struct wl_registry *reg, uint32_t name,
                            const char *iface, uint32_t ver) {
  Plat *p = (Plat *)data;
  (void)ver;
  if (strcmp(iface, "wl_compositor") == 0)
    p->comp = wl_registry_bind(reg, name, &wl_compositor_interface, 4);
  else if (strcmp(iface, "wl_shm") == 0)
    p->shm = wl_registry_bind(reg, name, &wl_shm_interface, 1);
  else if (strcmp(iface, "xdg_wm_base") == 0) {
    p->xdg = wl_registry_bind(reg, name, &xdg_wm_base_interface, 1);
    xdg_wm_base_add_listener(p->xdg, &xdg_wm_base_listener, p);
  } else if (strcmp(iface, "wl_seat") == 0) {
    p->seat = wl_registry_bind(reg, name, &wl_seat_interface, 5);
    wl_seat_add_listener(p->seat, &seat_listener, p);
  }
}
static void registry_remove(void *d, struct wl_registry *r, uint32_t n) {
  (void)d;
  (void)r;
  (void)n;
}
static const struct wl_registry_listener registry_listener = {registry_global,
                                                             registry_remove};

Plat *plat_create(const char *title, int w, int h) {
  Plat *p = (Plat *)calloc(1, sizeof(Plat));
  if (!p) return NULL;
  p->slot[0].fd = p->slot[1].fd = -1;
  p->running = 1;
  p->dpy = wl_display_connect(NULL);
  if (!p->dpy) {
    free(p);
    return NULL;
  }
  p->xkb_ctx = xkb_context_new(XKB_CONTEXT_NO_FLAGS);
  p->reg = wl_display_get_registry(p->dpy);
  wl_registry_add_listener(p->reg, &registry_listener, p);
  wl_display_roundtrip(p->dpy);
  if (!p->comp || !p->shm || !p->xdg) {
    plat_destroy(p);
    return NULL;
  }
  soft_set_font_px(&p->fb, 16);
  if (!soft_resize(&p->fb, w, h) || !make_bufs(p)) {
    plat_destroy(p);
    return NULL;
  }
  p->surf = wl_compositor_create_surface(p->comp);
  p->xdgs = xdg_wm_base_get_xdg_surface(p->xdg, p->surf);
  xdg_surface_add_listener(p->xdgs, &xdg_surface_listener, p);
  p->top = xdg_surface_get_toplevel(p->xdgs);
  xdg_toplevel_add_listener(p->top, &xdg_toplevel_listener, p);
  xdg_toplevel_set_title(p->top, title ? title : "mote");
  xdg_toplevel_set_app_id(p->top, "mote");
  xdg_toplevel_set_min_size(p->top, 200, 120);
  xdg_surface_set_window_geometry(p->xdgs, 0, 0, p->fb.w, p->fb.h);
  {
    struct wl_region *opaque = wl_compositor_create_region(p->comp);
    if (opaque) {
      wl_region_add(opaque, 0, 0, p->fb.w, p->fb.h);
      wl_surface_set_opaque_region(p->surf, opaque);
      wl_region_destroy(opaque);
    }
  }
  wl_surface_commit(p->surf);
  while (!p->configured) wl_display_dispatch(p->dpy);
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
  free(p->clip);
  destroy_bufs(p);
  if (p->kb) wl_keyboard_destroy(p->kb);
  if (p->ptr) wl_pointer_destroy(p->ptr);
  if (p->top) xdg_toplevel_destroy(p->top);
  if (p->xdgs) xdg_surface_destroy(p->xdgs);
  if (p->surf) wl_surface_destroy(p->surf);
  if (p->seat) wl_seat_destroy(p->seat);
  if (p->xdg) xdg_wm_base_destroy(p->xdg);
  if (p->shm) wl_shm_destroy(p->shm);
  if (p->comp) wl_compositor_destroy(p->comp);
  if (p->reg) wl_registry_destroy(p->reg);
  if (p->xkb_state) xkb_state_unref(p->xkb_state);
  if (p->xkb_map) xkb_keymap_unref(p->xkb_map);
  if (p->xkb_ctx) xkb_context_unref(p->xkb_ctx);
  if (p->dpy) wl_display_disconnect(p->dpy);
  soft_free(&p->fb);
  free(p);
}

void plat_wait(Plat *p) {
  struct pollfd pfd;
  if (p->qn > 0) return;
  while (wl_display_prepare_read(p->dpy) != 0) wl_display_dispatch_pending(p->dpy);
  wl_display_flush(p->dpy);
  pfd.fd = wl_display_get_fd(p->dpy);
  pfd.events = POLLIN;
  poll(&pfd, 1, -1);
  wl_display_read_events(p->dpy);
  wl_display_dispatch_pending(p->dpy);
}

mote_bool plat_poll(Plat *p, PlatEvent *ev) {
  wl_display_dispatch_pending(p->dpy);
  if (qpop(p, ev)) return MOTE_TRUE;
  if (!p->running) {
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
  size_t i, n;
  struct WlBuf *slot;
  soft_blit_caret(&p->fb);
  if (!p->fb.px || !p->surf) return;
  if (!make_bufs(p)) return;
  /* Wait until a SHM slot is free — never write into a buffer the compositor
   * is still reading (single-buffer caused torn / striped frames). */
  while (!(slot = free_slot(p))) {
    if (wl_display_dispatch(p->dpy) == -1) return;
  }
  n = (size_t)p->fb.w * (size_t)p->fb.h;
  if (n * 4 > slot->size) return;
  {
    unsigned int *dst = (unsigned int *)slot->data;
    for (i = 0; i < n; i++)
      dst[i] = (unsigned int)(p->fb.px[i] | 0xFF000000u);
  }
  {
    static int dumped;
    const char *dump = getenv("MOTE_DUMP_FB");
    /* Prefer soft FB dump — nested Weston/GL scrot invents stripes. */
    if (dump && !dumped) {
      FILE *f = fopen(dump, "wb");
      int x, y;
      if (f) {
        fprintf(f, "P6\n%d %d\n255\n", p->fb.w, p->fb.h);
        for (y = 0; y < p->fb.h; y++)
          for (x = 0; x < p->fb.w; x++) {
            mote_u32 c = p->fb.px[(size_t)y * (size_t)p->fb.w + (size_t)x];
            unsigned char rgb[3] = {(c >> 16) & 255, (c >> 8) & 255, c & 255};
            fwrite(rgb, 1, 3, f);
          }
        fclose(f);
      }
      dumped = 1;
    }
  }
  slot->busy = 1;
  wl_surface_attach(p->surf, slot->buf, 0, 0);
  wl_surface_damage_buffer(p->surf, 0, 0, p->fb.w, p->fb.h);
  {
    struct wl_region *opaque = wl_compositor_create_region(p->comp);
    if (opaque) {
      wl_region_add(opaque, 0, 0, p->fb.w, p->fb.h);
      wl_surface_set_opaque_region(p->surf, opaque);
      wl_region_destroy(opaque);
    }
  }
  wl_surface_commit(p->surf);
  wl_display_flush(p->dpy);
}
void plat_set_title(Plat *p, const char *title) {
  if (p->top) xdg_toplevel_set_title(p->top, title ? title : "mote");
}
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
