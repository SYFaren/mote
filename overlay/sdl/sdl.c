/* mote overlay/sdl — SDL2 (default) or SDL3 (-DMOTE_SDL3) */
#include "platform.h"
#include "soft.h"
#include "soft_keys.h"

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

/* Native SDL2: blit soft FB → window surface (1:1). Renderer path is for
 * SDL3 / Emscripten where GetWindowSurface is missing or unreliable. */
#if defined(MOTE_SDL3) || defined(__EMSCRIPTEN__)
#define MOTE_SDL_RENDERER 1
#endif

struct Plat {
  SoftFb fb;
  SDL_Window *win;
#ifdef MOTE_SDL_RENDERER
  SDL_Renderer *ren;
  SDL_Texture *tex;
#endif
  char *clip;
  size_t clip_n;
  PlatEvent q[128];
  int qn;
  mote_bool quit;
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

static void key_nav(Plat *p, PlatKey k, mote_bool ctrl, mote_bool shift) {
  PlatEvent e;
  memset(&e, 0, sizeof e);
  e.type = PE_KEY;
  e.key = k;
  e.ctrl = ctrl;
  e.shift = shift;
  qpush(p, &e);
}

#if defined(MOTE_SDL3)
static void map_key(Plat *p, const SDL_KeyboardEvent *ke) {
  mote_bool ctrl = (ke->mod & SDL_KMOD_CTRL) != 0;
  mote_bool shift = (ke->mod & SDL_KMOD_SHIFT) != 0;
  mote_bool alt = (ke->mod & SDL_KMOD_ALT) != 0;
  SDL_Keycode k = ke->key;
#else
static void map_key(Plat *p, const SDL_KeyboardEvent *ke) {
  mote_bool ctrl = (ke->keysym.mod & KMOD_CTRL) != 0;
  mote_bool shift = (ke->keysym.mod & KMOD_SHIFT) != 0;
  mote_bool alt = (ke->keysym.mod & KMOD_ALT) != 0;
  SDL_Keycode k = ke->keysym.sym;
#endif
  PlatKey pk = PK_NONE;
  if (ctrl || alt) {
    if (k >= SDLK_a && k <= SDLK_z) {
      pk = soft_ctrl_letter((int)('A' + (k - SDLK_a)), shift, alt);
      if (pk != PK_NONE) { key_nav(p, pk, ctrl, shift); return; }
    }
    if (k == SDLK_EQUALS || k == SDLK_PLUS) { key_nav(p, PK_ZOOMIN, 1, shift); return; }
    if (k == SDLK_MINUS) { key_nav(p, PK_ZOOMOUT, 1, shift); return; }
    if (k == SDLK_0) { key_nav(p, PK_ZOOMRESET, 1, shift); return; }
    if (k == SDLK_RIGHTBRACKET) { key_nav(p, PK_BRACKET, 1, shift); return; }
    if (k == SDLK_TAB) {
      key_nav(p, shift ? PK_PREVDOC : PK_NEXTDOC, 1, shift);
      return;
    }
  }
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
  case SDLK_F1: pk = PK_F1; break;
  case SDLK_F3: pk = shift ? PK_FINDPREV : PK_FINDNEXT; break;
  case SDLK_F4: if (ctrl) pk = PK_CLOSEDOC; break;
  case SDLK_F5: pk = PK_RELOAD; break;
  case SDLK_F7: pk = PK_WS; break;
  default: break;
  }
  if (pk != PK_NONE) key_nav(p, pk, ctrl, shift);
}

#ifdef MOTE_SDL_RENDERER
static mote_bool ensure_tex(Plat *p) {
  if (p->tex) {
    SDL_DestroyTexture(p->tex);
    p->tex = NULL;
  }
  if (p->fb.w < 1 || p->fb.h < 1) return MOTE_FALSE;
  /* Soft FB is 0x00RRGGBB → XRGB8888. ARGB+A=0 caused stripes on some GPUs. */
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

/* Keep soft FB pixel-identical to the drawable — avoids fractional scale stripes. */
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
  if (w < 200) w = 200;
  if (h < 120) h = 120;
  if (w == p->fb.w && h == p->fb.h) return MOTE_FALSE;
  soft_resize(&p->fb, w, h);
#ifdef MOTE_SDL_RENDERER
  ensure_tex(p);
#endif
  return MOTE_TRUE;
}

#ifdef __EMSCRIPTEN__
/* Match soft FB + SDL window to the visible #stage CSS box. */
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
  if (w < 200) w = 200;
  if (h < 120) h = 120;
  /* Do not touch the canvas / window when size is unchanged — calling
   * set_canvas_element_size every poll clears the bitmap and flickers. */
  if (w == p->fb.w && h == p->fb.h) return MOTE_FALSE;
  emscripten_set_canvas_element_size("#canvas", w, h);
  soft_resize(&p->fb, w, h);
  ensure_tex(p);
  SDL_SetWindowSize(p->win, w, h);
  return MOTE_TRUE;
}
#endif

Plat *plat_create(const char *title, int w, int h) {
  Plat *p = (Plat *)calloc(1, sizeof(Plat));
  if (!p) return NULL;
#if !defined(MOTE_SDL3)
  SDL_SetHint(SDL_HINT_RENDER_SCALE_QUALITY, "0"); /* nearest */
  SDL_SetHint(SDL_HINT_VIDEO_HIGHDPI_DISABLED, "1");
#endif
#if defined(MOTE_SDL3)
  if (!SDL_Init(SDL_INIT_VIDEO)) { free(p); return NULL; }
  p->win = SDL_CreateWindow(title ? title : "mote", w, h, SDL_WINDOW_RESIZABLE);
  if (!p->win) { SDL_Quit(); free(p); return NULL; }
  p->ren = SDL_CreateRenderer(p->win, NULL);
  if (!p->ren) {
    SDL_DestroyWindow(p->win);
    SDL_Quit();
    free(p);
    return NULL;
  }
#elif defined(MOTE_SDL_RENDERER)
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { free(p); return NULL; }
  p->win = SDL_CreateWindow(title ? title : "mote", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_RESIZABLE);
  if (!p->win) { SDL_Quit(); free(p); return NULL; }
  /* Prefer accelerated+vsync. SOFTWARE first was used historically and made
   * the Emscripten build crawl / flicker. */
  p->ren = SDL_CreateRenderer(p->win, -1,
                              SDL_RENDERER_ACCELERATED | SDL_RENDERER_PRESENTVSYNC);
  if (!p->ren)
    p->ren = SDL_CreateRenderer(p->win, -1, SDL_RENDERER_PRESENTVSYNC);
  if (!p->ren) p->ren = SDL_CreateRenderer(p->win, -1, 0);
  if (!p->ren) {
    SDL_DestroyWindow(p->win);
    SDL_Quit();
    free(p);
    return NULL;
  }
#else
  if (SDL_Init(SDL_INIT_VIDEO | SDL_INIT_EVENTS) != 0) { free(p); return NULL; }
  p->win = SDL_CreateWindow(title ? title : "mote", SDL_WINDOWPOS_CENTERED,
                            SDL_WINDOWPOS_CENTERED, w, h, SDL_WINDOW_RESIZABLE);
  if (!p->win) { SDL_Quit(); free(p); return NULL; }
#endif
  soft_set_font_px(&p->fb, 16);
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
  if (p->qn > 0) return;
#if defined(MOTE_SDL3)
  SDL_WaitEvent(NULL);
#else
  SDL_WaitEvent(NULL);
#endif
}

mote_bool plat_poll(Plat *p, PlatEvent *ev) {
  SDL_Event se;
#ifdef __EMSCRIPTEN__
  if (sync_em_canvas(p)) {
    memset(ev, 0, sizeof *ev);
    ev->type = PE_EXPOSE;
    return MOTE_TRUE;
  }
#endif
  if (qpop(p, ev)) return MOTE_TRUE;
  while (SDL_PollEvent(&se)) {
#if defined(MOTE_SDL3)
    if (se.type == SDL_EVENT_QUIT) {
      p->quit = MOTE_TRUE;
      ev->type = PE_QUIT;
      return MOTE_TRUE;
    }
    if (se.type == SDL_EVENT_WINDOW_RESIZED) {
      soft_resize(&p->fb, se.window.data1, se.window.data2);
#ifdef MOTE_SDL_RENDERER
      ensure_tex(p);
#endif
      sync_drawable_size(p);
      ev->type = PE_EXPOSE;
      return MOTE_TRUE;
    }
    if (se.type == SDL_EVENT_WINDOW_EXPOSED) {
      ev->type = PE_EXPOSE;
      return MOTE_TRUE;
    }
    if (se.type == SDL_EVENT_KEY_DOWN) {
      map_key(p, &se.key);
      if (qpop(p, ev)) return MOTE_TRUE;
    }
    if (se.type == SDL_EVENT_TEXT_INPUT) {
      PlatEvent te;
      size_t n = strlen(se.text.text);
      memset(&te, 0, sizeof te);
      te.type = PE_TEXT;
      if (n > sizeof te.text) n = sizeof te.text;
      memcpy(te.text, se.text.text, n);
      te.text_len = (int)n;
      qpush(p, &te);
      if (qpop(p, ev)) return MOTE_TRUE;
    }
    if (se.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
        se.type == SDL_EVENT_MOUSE_BUTTON_UP) {
      memset(ev, 0, sizeof *ev);
      ev->type = se.type == SDL_EVENT_MOUSE_BUTTON_DOWN ? PE_MOUSE_DOWN
                                                        : PE_MOUSE_UP;
      ev->mx = (int)se.button.x;
      ev->my = (int)se.button.y;
      return MOTE_TRUE;
    }
    if (se.type == SDL_EVENT_MOUSE_MOTION) {
      memset(ev, 0, sizeof *ev);
      ev->type = PE_MOUSE_MOVE;
      ev->mx = (int)se.motion.x;
      ev->my = (int)se.motion.y;
      return MOTE_TRUE;
    }
    if (se.type == SDL_EVENT_MOUSE_WHEEL) {
      memset(ev, 0, sizeof *ev);
      ev->type = PE_SCROLL;
      ev->wheel = (int)se.wheel.y;
      return MOTE_TRUE;
    }
#else
    if (se.type == SDL_QUIT) {
      p->quit = MOTE_TRUE;
      ev->type = PE_QUIT;
      return MOTE_TRUE;
    }
    if (se.type == SDL_WINDOWEVENT) {
#ifdef __EMSCRIPTEN__
      /* Size is owned by sync_em_canvas (CSS box). Ignoring SDL resize
       * events avoids a SetWindowSize ↔ SIZE_CHANGED feedback loop. */
      if (se.window.event == SDL_WINDOWEVENT_EXPOSED) {
        ev->type = PE_EXPOSE;
        return MOTE_TRUE;
      }
#else
      if (se.window.event == SDL_WINDOWEVENT_SIZE_CHANGED) {
        soft_resize(&p->fb, se.window.data1, se.window.data2);
#ifdef MOTE_SDL_RENDERER
        ensure_tex(p);
#endif
        sync_drawable_size(p);
        ev->type = PE_EXPOSE;
        return MOTE_TRUE;
      }
      if (se.window.event == SDL_WINDOWEVENT_EXPOSED) {
        ev->type = PE_EXPOSE;
        return MOTE_TRUE;
      }
#endif
    }
    if (se.type == SDL_KEYDOWN) {
      map_key(p, &se.key);
      if (qpop(p, ev)) return MOTE_TRUE;
    }
    if (se.type == SDL_TEXTINPUT) {
      PlatEvent te;
      size_t n = strlen(se.text.text);
      memset(&te, 0, sizeof te);
      te.type = PE_TEXT;
      if (n > sizeof te.text) n = sizeof te.text;
      memcpy(te.text, se.text.text, n);
      te.text_len = (int)n;
      qpush(p, &te);
      if (qpop(p, ev)) return MOTE_TRUE;
    }
    if (se.type == SDL_MOUSEBUTTONDOWN || se.type == SDL_MOUSEBUTTONUP) {
      memset(ev, 0, sizeof *ev);
      ev->type = se.type == SDL_MOUSEBUTTONDOWN ? PE_MOUSE_DOWN : PE_MOUSE_UP;
      ev->mx = se.button.x;
      ev->my = se.button.y;
      return MOTE_TRUE;
    }
    if (se.type == SDL_MOUSEMOTION) {
      memset(ev, 0, sizeof *ev);
      ev->type = PE_MOUSE_MOVE;
      ev->mx = se.motion.x;
      ev->my = se.motion.y;
      return MOTE_TRUE;
    }
    if (se.type == SDL_MOUSEWHEEL) {
      memset(ev, 0, sizeof *ev);
      ev->type = PE_SCROLL;
      ev->wheel = se.wheel.y;
      return MOTE_TRUE;
    }
#endif
  }
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
  soft_blit_caret(&p->fb);
  if (!p->fb.px) return;
#ifdef MOTE_SDL_RENDERER
  {
    if (!p->tex) return;
    /* Contiguous soft FB → one UpdateTexture (per-pixel lock was a bottleneck). */
    if (SDL_UpdateTexture(p->tex, NULL, p->fb.px, p->fb.w * (int)sizeof(mote_u32)) != 0) {
      void *pixels;
      int pitch;
      if (SDL_LockTexture(p->tex, NULL, &pixels, &pitch) == 0) {
        int y;
        for (y = 0; y < p->fb.h; y++) {
          mote_u32 *src = p->fb.px + (size_t)y * (size_t)p->fb.w;
          unsigned char *row = (unsigned char *)pixels + y * pitch;
          memcpy(row, src, (size_t)p->fb.w * sizeof(mote_u32));
        }
        SDL_UnlockTexture(p->tex);
      }
    }
#if defined(MOTE_SDL3)
    SDL_RenderTexture(p->ren, p->tex, NULL, NULL);
#else
    SDL_RenderCopy(p->ren, p->tex, NULL, NULL);
#endif
    SDL_RenderPresent(p->ren);
  }
#else
  {
    SDL_Surface *ws = SDL_GetWindowSurface(p->win);
    int x, y;
    static int dumped;
    if (!ws) return;
    /* Optional: MOTE_DUMP_FB=/path.ppm writes soft FB (for shots / debug). */
    if (!dumped && getenv("MOTE_DUMP_FB")) {
      FILE *f = fopen(getenv("MOTE_DUMP_FB"), "wb");
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
    if (SDL_LockSurface(ws) == 0) {
      int bpp = ws->format->BytesPerPixel;
      for (y = 0; y < p->fb.h && y < ws->h; y++) {
        Uint8 *dst = (Uint8 *)ws->pixels + y * ws->pitch;
        mote_u32 *src = p->fb.px + (size_t)y * (size_t)p->fb.w;
        for (x = 0; x < p->fb.w && x < ws->w; x++) {
          mote_u32 c = src[x];
          Uint32 pix = SDL_MapRGB(ws->format, (c >> 16) & 255, (c >> 8) & 255, c & 255);
          if (bpp == 4)
            ((Uint32 *)dst)[x] = pix;
          else if (bpp == 3) {
            Uint8 *d = dst + x * 3;
            d[0] = (Uint8)(pix & 0xFF);
            d[1] = (Uint8)((pix >> 8) & 0xFF);
            d[2] = (Uint8)((pix >> 16) & 0xFF);
          } else if (bpp == 2)
            ((Uint16 *)dst)[x] = (Uint16)pix;
        }
      }
      SDL_UnlockSurface(ws);
    }
    SDL_UpdateWindowSurface(p->win);
  }
#endif
}
void plat_set_title(Plat *p, const char *title) {
#if defined(MOTE_SDL3)
  SDL_SetWindowTitle(p->win, title ? title : "mote");
#else
  SDL_SetWindowTitle(p->win, title ? title : "mote");
#endif
}
mote_bool plat_set_caret(Plat *p, int x, int y, int h, mote_bool on) {
  p->fb.caret_x = x;
  p->fb.caret_y = y;
  p->fb.caret_h = h;
  p->fb.caret_on = on;
  return MOTE_TRUE;
}
char *plat_clipboard_get(Plat *p, size_t *out_len) {
  char *t;
#if defined(MOTE_SDL3)
  t = SDL_GetClipboardText();
#else
  t = SDL_GetClipboardText();
#endif
  if (!t) {
    if (out_len) *out_len = 0;
    return NULL;
  }
  {
    size_t n = strlen(t);
    char *c = (char *)malloc(n + 1);
    if (!c) {
      SDL_free(t);
      if (out_len) *out_len = 0;
      return NULL;
    }
    memcpy(c, t, n + 1);
    SDL_free(t);
    if (out_len) *out_len = n;
    (void)p;
    return c;
  }
}
mote_bool plat_clipboard_set(Plat *p, const char *s, size_t n) {
  char *tmp;
  (void)p;
  tmp = (char *)malloc(n + 1);
  if (!tmp) return MOTE_FALSE;
  memcpy(tmp, s, n);
  tmp[n] = 0;
#if defined(MOTE_SDL3)
  {
    mote_bool ok = SDL_SetClipboardText(tmp) ? MOTE_TRUE : MOTE_FALSE;
    free(tmp);
    return ok;
  }
#else
  {
    int ok = SDL_SetClipboardText(tmp) == 0;
    free(tmp);
    return ok ? MOTE_TRUE : MOTE_FALSE;
  }
#endif
}
