#include "platform.h"
#include "keymap.h"
#include "soft.h"
#include "../evq.h"

#if defined(MOTE_SDL3)
#include <SDL3/SDL.h>
#else
#include <SDL.h>
#endif

#ifdef __EMSCRIPTEN__
#include <emscripten.h>
#include <emscripten/html5.h>
#endif

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

/* Desktop SDL2 copies the framebuffer straight to the window surface.
   SDL3 and the browser go through a renderer: the window surface is
   missing or unreliable there. */
#if defined(MOTE_SDL3) || defined(__EMSCRIPTEN__)
#define MOTE_SDL_RENDERER 1
#endif

/* SDL3 names for SDL2, so one code path serves both. SDL3 calls return bool
   (true = ok) where SDL2 returned int (0 = ok); SDL_OK hides that. */
#if defined(MOTE_SDL3)
#define SDL_OK(r) (r)
#define KEY_SYM(ke) ((ke)->key)
#define KEY_SCAN(ke) ((ke)->scancode)
#define KEY_MOD(ke) ((ke)->mod)
#else
#define SDL_OK(r) ((r) == 0)
#define KEY_SYM(ke) ((ke)->keysym.sym)
#define KEY_SCAN(ke) ((ke)->keysym.scancode)
#define KEY_MOD(ke) ((ke)->keysym.mod)
#define SDL_KMOD_CTRL KMOD_CTRL
#define SDL_KMOD_SHIFT KMOD_SHIFT
#define SDL_KMOD_ALT KMOD_ALT
#define SDL_EVENT_QUIT SDL_QUIT
#define SDL_EVENT_KEY_DOWN SDL_KEYDOWN
#define SDL_EVENT_TEXT_INPUT SDL_TEXTINPUT
#define SDL_EVENT_MOUSE_BUTTON_DOWN SDL_MOUSEBUTTONDOWN
#define SDL_EVENT_MOUSE_BUTTON_UP SDL_MOUSEBUTTONUP
#define SDL_EVENT_MOUSE_MOTION SDL_MOUSEMOTION
#define SDL_EVENT_MOUSE_WHEEL SDL_MOUSEWHEEL
#endif

struct Plat {
  SoftFb fb;
  SDL_Window *win;
#ifdef MOTE_SDL_RENDERER
  SDL_Renderer *ren;
  SDL_Texture *tex;
#endif
  EvQueue q;
};

/* ASCII of a key for the shared keymap. Printable SDL keycodes are their
   ASCII; on non-Latin layouts fall back to the letter's scancode. */
static int key_char(SDL_Keycode k, SDL_Scancode sc) {
  switch (k) {
  case SDLK_KP_PLUS: return '+';
  case SDLK_KP_MINUS: return '-';
  case SDLK_KP_0: return '0';
  case SDLK_KP_ENTER: return '\r';
  default: break;
  }
  if (k > 0 && k < 0x7f) return (int)k;
  if (sc >= SDL_SCANCODE_A && sc <= SDL_SCANCODE_Z) return 'a' + (int)(sc - SDL_SCANCODE_A);
  return 0;
}

static void map_key(Plat *p, const SDL_KeyboardEvent *ke) {
  mote_bool ctrl = (KEY_MOD(ke) & SDL_KMOD_CTRL) != 0;
  mote_bool shift = (KEY_MOD(ke) & SDL_KMOD_SHIFT) != 0;
  mote_bool alt = (KEY_MOD(ke) & SDL_KMOD_ALT) != 0;
  SDL_Keycode k = KEY_SYM(ke);
  int ch = key_char(k, KEY_SCAN(ke));
  PlatKey pk = PK_NONE;
  PlatEvent e;
  if (alt && !ctrl && ch) pk = key_alt(ch);
  else if (ctrl && ch) pk = key_ctrl(ch, shift);
  if (pk == PK_NONE) {
    switch (k) {
    case SDLK_LEFT: pk = PK_LEFT; break;
    case SDLK_RIGHT: pk = PK_RIGHT; break;
    case SDLK_UP: pk = PK_UP; break;
    case SDLK_DOWN: pk = PK_DOWN; break;
    case SDLK_HOME: pk = PK_HOME; break;
    case SDLK_END: pk = PK_END; break;
    case SDLK_PAGEUP: pk = PK_PGUP; break;
    case SDLK_PAGEDOWN: pk = PK_PGDN; break;
    case SDLK_BACKSPACE: pk = PK_BACKSPACE; break;
    case SDLK_DELETE: pk = PK_DELETE; break;
    case SDLK_RETURN:
    case SDLK_KP_ENTER: pk = PK_ENTER; break;
    case SDLK_ESCAPE: pk = PK_ESCAPE; break;
    case SDLK_TAB: pk = PK_TAB; break;
    default:
      if (k >= SDLK_F1 && k <= SDLK_F12) pk = key_fn((int)(k - SDLK_F1) + 1, ctrl, shift);
      break;
    }
  }
  if (pk == PK_NONE) return;
  memset(&e, 0, sizeof e);
  e.type = PE_KEY;
  e.key = pk;
  e.ctrl = ctrl;
  e.shift = shift;
  evq_push(&p->q, &e);
}

#ifdef MOTE_SDL_RENDERER
static mote_bool ensure_tex(Plat *p) {
  if (p->tex) {
    SDL_DestroyTexture(p->tex);
    p->tex = NULL;
  }
  if (p->fb.w < 1 || p->fb.h < 1) return MOTE_FALSE;
  /* The framebuffer is 0x00RRGGBB, which is XRGB8888; ARGB with zero alpha
   shows stripes on some GPUs. */
  p->tex = SDL_CreateTexture(p->ren, SDL_PIXELFORMAT_XRGB8888,
                             SDL_TEXTUREACCESS_STREAMING, p->fb.w, p->fb.h);
  if (!p->tex)
    p->tex = SDL_CreateTexture(p->ren, SDL_PIXELFORMAT_ARGB8888,
                               SDL_TEXTUREACCESS_STREAMING, p->fb.w, p->fb.h);
  if (!p->tex) return MOTE_FALSE;
  SDL_SetTextureBlendMode(p->tex, SDL_BLENDMODE_NONE);
#if !defined(MOTE_SDL3)
  SDL_SetTextureScaleMode(p->tex, SDL_ScaleModeNearest);
#endif
  return MOTE_TRUE;
}
#endif

static void fb_resize(Plat *p, int w, int h) {
  if (w < MOTE_MIN_WIN_W) w = MOTE_MIN_WIN_W;
  if (h < MOTE_MIN_WIN_H) h = MOTE_MIN_WIN_H;
  soft_resize(&p->fb, w, h);
#ifdef MOTE_SDL_RENDERER
  ensure_tex(p);
#endif
}

/* Keep the framebuffer the same pixel size as the drawable, or fractional
   scaling draws stripes. */
static mote_bool sync_drawable_size(Plat *p) {
  int w = 0, h = 0;
#if defined(MOTE_SDL3)
  SDL_GetWindowSizeInPixels(p->win, &w, &h);
#elif defined(MOTE_SDL_RENDERER)
  if (p->ren) SDL_GetRendererOutputSize(p->ren, &w, &h);
  if (w < 1 || h < 1) SDL_GetWindowSize(p->win, &w, &h);
#else
  {
    SDL_Surface *ws = SDL_GetWindowSurface(p->win);
    if (ws) {
      w = ws->w;
      h = ws->h;
    } else
      SDL_GetWindowSize(p->win, &w, &h);
  }
#endif
  if (w < MOTE_MIN_WIN_W) w = MOTE_MIN_WIN_W;
  if (h < MOTE_MIN_WIN_H) h = MOTE_MIN_WIN_H;
  if (w == p->fb.w && h == p->fb.h) return MOTE_FALSE;
  fb_resize(p, w, h);
  return MOTE_TRUE;
}

#ifdef __EMSCRIPTEN__
/* Size the framebuffer and window to the visible #stage box on the page. */
static mote_bool sync_em_canvas(Plat *p) {
  double css_w = 0, css_h = 0;
  int w, h;
  if (emscripten_get_element_css_size("#stage", &css_w, &css_h) !=
      EMSCRIPTEN_RESULT_SUCCESS) {
    if (emscripten_get_element_css_size("#canvas", &css_w, &css_h) !=
        EMSCRIPTEN_RESULT_SUCCESS)
      return MOTE_FALSE;
  }
  w = (int)css_w;
  h = (int)css_h;
  if (w < MOTE_MIN_WIN_W) w = MOTE_MIN_WIN_W;
  if (h < MOTE_MIN_WIN_H) h = MOTE_MIN_WIN_H;
  /* Leave the canvas alone when the size is the same: setting it on every
     poll clears it and flickers. */
  if (w == p->fb.w && h == p->fb.h) return MOTE_FALSE;
  emscripten_set_canvas_element_size("#canvas", w, h);
  fb_resize(p, w, h);
  SDL_SetWindowSize(p->win, w, h);
  return MOTE_TRUE;
}
#endif

static void fail_create(Plat *p) {
#ifdef MOTE_SDL_RENDERER
  if (p->ren) SDL_DestroyRenderer(p->ren);
#endif
  if (p->win) SDL_DestroyWindow(p->win);
  SDL_Quit();
  free(p);
}

Plat *plat_create(const char *title, int w, int h) {
  Plat *p = (Plat *)calloc(1, sizeof(Plat));
  if (!title) title = "mote";
  if (!p) return NULL;
#if defined(MOTE_SDL3)
  if (!SDL_Init(SDL_INIT_VIDEO)) { free(p); return NULL; }
  p->win = SDL_CreateWindow(title, w, h, SDL_WINDOW_RESIZABLE);
  if (p->win) p->ren = SDL_CreateRenderer(p->win, NULL);
  if (!p->ren) { fail_create(p); return NULL; }
#else
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); /* nearest */
  SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { free(p); return NULL; }
  p->win = SDL_CreateWindow(title, SDL_WINDOWPOS_CENTERED, SDL_WINDOWPOS_CENTERED, w, h,
                            SDL_WINDOW_RESIZABLE);
  if (!p->win) { fail_create(p); return NULL; }
#ifdef MOTE_SDL_RENDERER
  /* Accelerated with vsync first; the software renderer makes the browser
     build slow and flickery. */
  p->ren = SDL_CreateRenderer(p->win, -1,
                              SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!p->ren) p->ren = SDL_CreateRenderer(p->win, -1, SDL_RENDERER_PRESENTVSYNC);
  if (!p->ren) p->ren = SDL_CreateRenderer(p->win, -1, 0);
  if (!p->ren) { fail_create(p); return NULL; }
#endif
#endif
  soft_set_font_px(&p->fb, MOTE_FONT_PX);
  if (!soft_resize(&p->fb, w, h)) {
    plat_destroy(p);
    return NULL;
  }
#ifdef MOTE_SDL_RENDERER
  if (!ensure_tex(p)) {
    plat_destroy(p);
    return NULL;
  }
#endif
  sync_drawable_size(p);
#ifdef __EMSCRIPTEN__
  sync_em_canvas(p);
#endif
#if defined(MOTE_SDL3)
  SDL_StartTextInput(p->win);
#else
  SDL_StartTextInput();
#endif
  evq_type(&p->q, PE_EXPOSE);
  return p;
}

void plat_destroy(Plat *p) {
  if (!p) return;
#ifdef MOTE_SDL_RENDERER
  if (p->tex) SDL_DestroyTexture(p->tex);
  if (p->ren) SDL_DestroyRenderer(p->ren);
#endif
  if (p->win) SDL_DestroyWindow(p->win);
  soft_free(&p->fb);
  SDL_Quit();
  free(p);
}

void plat_wait(Plat *p) {
  if (p->q.n > 0) return;
#ifdef __EMSCRIPTEN__
  /* Don't wait forever: the page layout often settles after the first
     ed_draw, and a blocking wait would leave a black canvas until a click.
     The timeout lets the size sync send PE_EXPOSE. */
  if (sync_em_canvas(p)) {
    evq_type(&p->q, PE_EXPOSE);
    return;
  }
  SDL_WaitEventTimeout(NULL, 32);
  if (sync_em_canvas(p)) evq_type(&p->q, PE_EXPOSE);
#else
  SDL_WaitEvent(NULL);
#endif
}

/* Resize/expose window events; MOTE_TRUE if the editor must redraw. */
static mote_bool window_event(Plat *p, const SDL_Event *se) {
#if defined(MOTE_SDL3)
  if (se->type == SDL_EVENT_WINDOW_RESIZED) {
    fb_resize(p, se->window.data1, se->window.data2);
    sync_drawable_size(p);
    return MOTE_TRUE;
  }
  return se->type == SDL_EVENT_WINDOW_EXPOSED;
#else
  if (se->type != SDL_WINDOWEVENT) return MOTE_FALSE;
#ifndef __EMSCRIPTEN__
  /* In the browser sync_em_canvas owns the size; following SDL resize events
     as well would bounce between SetWindowSize and SIZE_CHANGED. */
  if (se->window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
    fb_resize(p, se->window.data1, se->window.data2);
    sync_drawable_size(p);
    return MOTE_TRUE;
  }
#endif
  return se->window.event == SDL_WINDOWEVENT_EXPOSED;
#endif
}

/* Translate one SDL event; MOTE_FALSE if it produced nothing. */
static mote_bool translate(Plat *p, const SDL_Event *se, PlatEvent *ev) {
  memset(ev, 0, sizeof *ev);
  if (window_event(p, se)) {
    ev->type = PE_EXPOSE;
    return MOTE_TRUE;
  }
  switch (se->type) {
  case SDL_EVENT_QUIT:
    ev->type = PE_QUIT;
    return MOTE_TRUE;
  case SDL_EVENT_KEY_DOWN:
    map_key(p, &se->key);
    return evq_pop(&p->q, ev);
  case SDL_EVENT_TEXT_INPUT: {
    size_t n = strlen(se->text.text);
    if (n > sizeof ev->text) n = sizeof ev->text;
    ev->type = PE_TEXT;
    memcpy(ev->text, se->text.text, n);
    ev->text_len = (int)n;
    return MOTE_TRUE;
  }
  case SDL_EVENT_MOUSE_BUTTON_DOWN:
  case SDL_EVENT_MOUSE_BUTTON_UP:
    ev->type = se->type == SDL_EVENT_MOUSE_BUTTON_DOWN ? PE_MOUSE_DOWN : PE_MOUSE_UP;
    ev->mx = (int)se->button.x;
    ev->my = (int)se->button.y;
    return MOTE_TRUE;
  case SDL_EVENT_MOUSE_MOTION:
    ev->type = PE_MOUSE_MOVE;
    ev->mx = (int)se->motion.x;
    ev->my = (int)se->motion.y;
    return MOTE_TRUE;
  case SDL_EVENT_MOUSE_WHEEL:
    ev->type = PE_SCROLL;
    ev->wheel = (int)se->wheel.y;
    return MOTE_TRUE;
  default:
    return MOTE_FALSE;
  }
}

mote_bool plat_poll(Plat *p, PlatEvent *ev) {
  SDL_Event se;
  memset(ev, 0, sizeof *ev);
#ifdef __EMSCRIPTEN__
  if (sync_em_canvas(p)) {
    ev->type = PE_EXPOSE;
    return MOTE_TRUE;
  }
#endif
  if (evq_pop(&p->q, ev)) return MOTE_TRUE;
  while (SDL_PollEvent(&se))
    if (translate(p, &se, ev)) return MOTE_TRUE;
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

#ifdef MOTE_SDL_RENDERER
static void present(Plat *p) {
  void *pixels;
  int pitch, y, x;
  if (!p->tex) return;
  /* Lock and set alpha: the browser canvas treats zero alpha as transparent,
     so plain 0x00RRGGBB pixels show a black page. */
  if (SDL_OK(SDL_LockTexture(p->tex, NULL, &pixels, &pitch))) {
    for (y = 0; y < p->fb.h; y++) {
      const mote_u32 *src = p->fb.px + (size_t)y * (size_t)p->fb.w;
      unsigned char *row = (unsigned char *)pixels + y * pitch;
      for (x = 0; x < p->fb.w; x++) {
        mote_u32 c = src[x] | 0xFF000000u;
        memcpy(row + x * 4, &c, 4); /* pitch may leave rows unaligned */
      }
    }
    SDL_UnlockTexture(p->tex);
  }
#if defined(MOTE_SDL3)
  SDL_RenderTexture(p->ren, p->tex, NULL, NULL);
#else
  SDL_RenderCopy(p->ren, p->tex, NULL, NULL);
#endif
  SDL_RenderPresent(p->ren);
}
#else
static void present(Plat *p) {
  SDL_Surface *ws = SDL_GetWindowSurface(p->win);
  int x, y, bpp;
  if (!ws) return;
  soft_dump_once(&p->fb);
  if (SDL_LockSurface(ws) != 0) return;
  bpp = ws->format->BytesPerPixel;
  for (y = 0; y < p->fb.h && y < ws->h; y++) {
    Uint8 *dst = (Uint8 *)ws->pixels + y * ws->pitch;
    const mote_u32 *src = p->fb.px + (size_t)y * (size_t)p->fb.w;
    for (x = 0; x < p->fb.w && x < ws->w; x++) {
      mote_u32 c = src[x];
      Uint32 pix = SDL_MapRGB(ws->format, (c >> 16) & 255, (c >> 8) & 255, c & 255);
      if (bpp == 4) {
        ((Uint32 *)dst)[x] = pix;
      } else if (bpp == 3) {
        dst[x * 3] = (Uint8)pix;
        dst[x * 3 + 1] = (Uint8)(pix >> 8);
        dst[x * 3 + 2] = (Uint8)(pix >> 16);
      } else if (bpp == 2) {
        ((Uint16 *)dst)[x] = (Uint16)pix;
      }
    }
  }
  SDL_UnlockSurface(ws);
  SDL_UpdateWindowSurface(p->win);
}
#endif

void plat_end_frame(Plat *p) {
  soft_blit_caret(&p->fb);
  if (p->fb.px) present(p);
}
void plat_set_title(Plat *p, const char *title) {
  SDL_SetWindowTitle(p->win, title ? title : "mote");
}
mote_bool plat_set_caret(Plat *p, int x, int y, int h, mote_bool on) {
  p->fb.caret_x = x;
  p->fb.caret_y = y;
  p->fb.caret_h = h;
  p->fb.caret_on = on;
  return MOTE_TRUE;
}
char *plat_clipboard_get(Plat *p, size_t *out_len) {
  char *t = SDL_GetClipboardText(), *c = NULL;
  size_t n = t ? strlen(t) : 0;
  (void)p;
  if (n) c = (char *)malloc(n + 1);
  if (c) memcpy(c, t, n + 1);
  if (t) SDL_free(t);
  if (out_len) *out_len = c ? n : 0;
  return c;
}
mote_bool plat_clipboard_set(Plat *p, const char *s, size_t n) {
  char *tmp = (char *)malloc(n + 1);
  mote_bool ok;
  (void)p;
  if (!tmp) return MOTE_FALSE;
  memcpy(tmp, s, n);
  tmp[n] = 0;
  ok = SDL_OK(SDL_SetClipboardText(tmp)) ? MOTE_TRUE : MOTE_FALSE;
  free(tmp);
  return ok;
}
