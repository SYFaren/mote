/* mote overlay/wayland — wl_shm + xdg-shell + xkbcommon */
#include "platform.h"
#include "soft.h"
#include "../evq.h"
#include "keymap.h"

#include "xdg-decoration-unstable-v1-client-protocol.h"
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
#include <time.h>
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
  struct zxdg_decoration_manager_v1 *deco_mgr;
  struct zxdg_toplevel_decoration_v1 *deco;
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
  EvQueue q;
  mote_bool ctrl, shift, alt;
  /* Wayland leaves key repeat to the client */
  int rep_rate, rep_delay;  /* keys per second, ms before the first repeat */
  xkb_keycode_t rep_key;    /* held key being repeated, 0 = none */
  long long rep_next;       /* monotonic ms of the next repeat */
};

static void key_nav(Plat *p, PlatKey k) {
  evq_key(&p->q, k, p->ctrl, p->shift);
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
  (void)top;
  (void)states;
  /* Compositor may pass 0,0 = "client chooses". Keep current size then. */
  if (w > 0 && h > 0) {
    if (w < MOTE_MIN_WIN_W) w = MOTE_MIN_WIN_W;
    if (h < MOTE_MIN_WIN_H) h = MOTE_MIN_WIN_H;
    if (w != p->fb.w || h != p->fb.h) {
      soft_resize(&p->fb, w, h);
      make_bufs(p);
    }
    if (p->xdgs)
      xdg_surface_set_window_geometry(p->xdgs, 0, 0, p->fb.w, p->fb.h);
  }
  evq_type(&p->q, PE_EXPOSE);
}
static void xdg_toplevel_close(void *data, struct xdg_toplevel *top) {
  Plat *p = (Plat *)data;
  PlatEvent e;
  (void)top;
  p->running = 0;
  memset(&e, 0, sizeof e);
  e.type = PE_QUIT;
  evq_push(&p->q, &e);
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
static void kb_leave(void *data, struct wl_keyboard *k, uint32_t s, struct wl_surface *sf) {
  ((Plat *)data)->rep_key = 0;
  (void)k;
  (void)s;
  (void)sf;
}

/* ASCII for a keysym as the shared keymap sees it (keypad folded in). */
static int sym_char(xkb_keysym_t sym) {
  switch (sym) {
  case XKB_KEY_KP_Add: return '+';
  case XKB_KEY_KP_Subtract: return '-';
  case XKB_KEY_KP_0: return '0';
  case XKB_KEY_Tab:
  case XKB_KEY_ISO_Left_Tab: return '\t';
  case XKB_KEY_Return:
  case XKB_KEY_KP_Enter: return '\r';
  default: return sym >= 0x20 && sym < 0x7f ? (int)sym : 0;
  }
}

/* Shortcut or navigation key → PE_KEY; MOTE_FALSE if it should type text. */
static mote_bool map_sym(Plat *p, xkb_keycode_t code, xkb_keysym_t sym) {
  int ch = sym_char(sym);
  PlatKey pk = PK_NONE;
  if (!ch && (p->ctrl || p->alt)) {
    /* non-Latin layout: use the key's symbol in the first layout */
    const xkb_keysym_t *syms;
    if (xkb_keymap_key_get_syms_by_level(p->xkb_map, code, 0, 0, &syms) > 0)
      ch = sym_char(syms[0]);
  }
  if (p->alt && !p->ctrl && ch) pk = key_alt(ch);
  else if (p->ctrl && ch) pk = key_ctrl(ch, p->shift);
  if (pk == PK_NONE) {
    switch (sym) {
    case XKB_KEY_Left: pk = PK_LEFT; break;
    case XKB_KEY_Right: pk = PK_RIGHT; break;
    case XKB_KEY_Up: pk = PK_UP; break;
    case XKB_KEY_Down: pk = PK_DOWN; break;
    case XKB_KEY_Home: pk = PK_HOME; break;
    case XKB_KEY_End: pk = PK_END; break;
    case XKB_KEY_Page_Up: pk = PK_PGUP; break;
    case XKB_KEY_Page_Down: pk = PK_PGDN; break;
    case XKB_KEY_BackSpace: pk = PK_BACKSPACE; break;
    case XKB_KEY_Delete: pk = PK_DELETE; break;
    case XKB_KEY_Return:
    case XKB_KEY_KP_Enter: pk = PK_ENTER; break;
    case XKB_KEY_Escape: pk = PK_ESCAPE; break;
    case XKB_KEY_Tab:
    case XKB_KEY_ISO_Left_Tab: pk = PK_TAB; break;
    default:
      if (sym >= XKB_KEY_F1 && sym <= XKB_KEY_F12)
        pk = key_fn((int)(sym - XKB_KEY_F1) + 1, p->ctrl, p->shift);
      break;
    }
  }
  if (pk != PK_NONE) key_nav(p, pk);
  return pk != PK_NONE || p->ctrl || p->alt;
}

static long long now_ms(void) {
  struct timespec ts;
  clock_gettime(CLOCK_MONOTONIC, &ts);
  return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

static void key_press(Plat *p, xkb_keycode_t code) {
  xkb_keysym_t sym = xkb_state_key_get_one_sym(p->xkb_state, code);
  char buf[32];
  int n;
  p->ctrl = xkb_state_mod_name_is_active(p->xkb_state, XKB_MOD_NAME_CTRL,
                                         XKB_STATE_MODS_EFFECTIVE) > 0;
  p->shift = xkb_state_mod_name_is_active(p->xkb_state, XKB_MOD_NAME_SHIFT,
                                          XKB_STATE_MODS_EFFECTIVE) > 0;
  p->alt = xkb_state_mod_name_is_active(p->xkb_state, XKB_MOD_NAME_ALT,
                                        XKB_STATE_MODS_EFFECTIVE) > 0;
  if (map_sym(p, code, sym)) return;
  n = xkb_state_key_get_utf8(p->xkb_state, code, buf, sizeof buf);
  if (n > 0) {
    if (n >= (int)sizeof buf) n = (int)sizeof buf - 1;
    evq_text(&p->q, buf, n);
    evq_flush_text(&p->q);
  }
}

static void kb_key(void *data, struct wl_keyboard *kb, uint32_t serial, uint32_t time,
                   uint32_t key, uint32_t state) {
  Plat *p = (Plat *)data;
  xkb_keycode_t code = key + 8;
  (void)kb;
  (void)serial;
  (void)time;
  if (!p->xkb_state) return;
  if (state != WL_KEYBOARD_KEY_STATE_PRESSED) {
    if (code == p->rep_key) p->rep_key = 0;
    return;
  }
  key_press(p, code);
  p->rep_key = 0;
  if (p->rep_rate > 0 && xkb_keymap_key_repeats(p->xkb_map, code)) {
    p->rep_key = code;
    p->rep_next = now_ms() + p->rep_delay;
  }
}

/* Fire the held key if its repeat is due. */
static void key_repeat_tick(Plat *p) {
  long long now;
  if (!p->rep_key) return;
  now = now_ms();
  if (now < p->rep_next) return;
  key_press(p, p->rep_key);
  p->rep_next += 1000 / p->rep_rate;
  if (p->rep_next < now) p->rep_next = now + 1000 / p->rep_rate; /* we were stalled */
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
static void kb_repeat(void *data, struct wl_keyboard *k, int32_t rate, int32_t delay) {
  Plat *p = (Plat *)data;
  (void)k;
  p->rep_rate = rate > 0 ? rate : 0;
  p->rep_delay = delay > 0 ? delay : 0;
  if (!p->rep_rate) p->rep_key = 0;
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
  evq_push(&p->q, &e);
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
  evq_push(&p->q, &e);
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
  evq_push(&p->q, &e);
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
/* Designated so newer headers' extra events (sent only to seat v9+) stay NULL. */
static const struct wl_pointer_listener ptr_listener = {
    .enter = ptr_enter,
    .leave = ptr_leave,
    .motion = ptr_motion,
    .button = ptr_button,
    .axis = ptr_axis,
    .frame = ptr_frame,
    .axis_source = ptr_axis_source,
    .axis_stop = ptr_axis_stop,
    .axis_discrete = ptr_axis_discrete,
    .axis_value120 = ptr_axis_value120};

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
  } else if (strcmp(iface, "zxdg_decoration_manager_v1") == 0) {
    p->deco_mgr = wl_registry_bind(reg, name, &zxdg_decoration_manager_v1_interface, 1);
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
  soft_set_font_px(&p->fb, MOTE_FONT_PX);
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
  xdg_toplevel_set_min_size(p->top, MOTE_MIN_WIN_W, MOTE_MIN_WIN_H);
  /* No CSD of our own: without server-side decorations (e.g. GNOME) the window stays bare. */
  if (p->deco_mgr) {
    p->deco = zxdg_decoration_manager_v1_get_toplevel_decoration(p->deco_mgr, p->top);
    zxdg_toplevel_decoration_v1_set_mode(p->deco,
                                         ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
  }
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
  evq_type(&p->q, PE_EXPOSE);
  return p;
}

void plat_destroy(Plat *p) {
  if (!p) return;
  free(p->clip);
  destroy_bufs(p);
  if (p->kb) wl_keyboard_destroy(p->kb);
  if (p->ptr) wl_pointer_destroy(p->ptr);
  if (p->deco) zxdg_toplevel_decoration_v1_destroy(p->deco);
  if (p->deco_mgr) zxdg_decoration_manager_v1_destroy(p->deco_mgr);
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
  int timeout = -1;
  if (p->q.n > 0) return;
  if (p->rep_key) {
    long long left = p->rep_next - now_ms();
    timeout = left > 0 ? (int)left : 0;
  }
  while (wl_display_prepare_read(p->dpy) != 0) wl_display_dispatch_pending(p->dpy);
  wl_display_flush(p->dpy);
  pfd.fd = wl_display_get_fd(p->dpy);
  pfd.events = POLLIN;
  if (poll(&pfd, 1, timeout) > 0) wl_display_read_events(p->dpy);
  else wl_display_cancel_read(p->dpy);
  wl_display_dispatch_pending(p->dpy);
}

mote_bool plat_poll(Plat *p, PlatEvent *ev) {
  wl_display_dispatch_pending(p->dpy);
  if (evq_pop(&p->q, ev)) return MOTE_TRUE;
  key_repeat_tick(p);
  if (evq_pop(&p->q, ev)) return MOTE_TRUE;
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
  soft_dump_once(&p->fb);
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
