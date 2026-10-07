/* mote soft RGB framebuffer */
#include "soft.h"
#include "utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font8x16.inc"
#include "font_cyr8x16.inc"

#define SOFT_MAX_SCALE 3
#define SOFT_MAX_DIM 4096

mote_bool soft_resize(SoftFb *fb, int w, int h) {
  mote_u32 *n;
  size_t sz;
  if (w < 1) w = 1;
  if (h < 1) h = 1;
  if (w > SOFT_MAX_DIM) w = SOFT_MAX_DIM;
  if (h > SOFT_MAX_DIM) h = SOFT_MAX_DIM;
  if (fb->px && fb->w == w && fb->h == h) return MOTE_TRUE;
  sz = (size_t)w * (size_t)h;
  n = (mote_u32 *)realloc(fb->px, sz * sizeof(mote_u32));
  if (!n) return MOTE_FALSE;
  /* Uninitialized growth looked like stripes/noise until the next full draw. */
  memset(n, 0, sz * sizeof(mote_u32));
  fb->px = n;
  fb->w = w;
  fb->h = h;
  return MOTE_TRUE;
}

void soft_free(SoftFb *fb) {
  free(fb->px);
  fb->px = NULL;
  fb->w = fb->h = 0;
}

/* The bitmap font only scales by whole multiples of 16px, so round toward the
   request: Ctrl+= from 16 asks for 17 and must land on 32, not back on 16. */
void soft_set_font_px(SoftFb *fb, int px) {
  int sc = px > fb->font_px ? (px + SOFT_FONT_H - 1) / SOFT_FONT_H : px / SOFT_FONT_H;
  if (sc < 1) sc = 1;
  if (sc > SOFT_MAX_SCALE) sc = SOFT_MAX_SCALE;
  fb->scale = sc;
  fb->font_px = sc * SOFT_FONT_H;
}

int soft_font_w(const SoftFb *fb) { return SOFT_FONT_W * (fb->scale > 0 ? fb->scale : 1); }
int soft_font_h(const SoftFb *fb) { return SOFT_FONT_H * (fb->scale > 0 ? fb->scale : 1); }

void soft_clear(SoftFb *fb, mote_u32 rgb) {
  size_t i, n;
  if (!fb->px) return;
  n = (size_t)fb->w * (size_t)fb->h;
  for (i = 0; i < n; i++) fb->px[i] = rgb;
}

void soft_fill_rect(SoftFb *fb, int x, int y, int w, int h, mote_u32 rgb) {
  int x0, y0, x1, y1, xx, yy;
  if (!fb->px || w <= 0 || h <= 0) return;
  x0 = x < 0 ? 0 : x;
  y0 = y < 0 ? 0 : y;
  x1 = x + w;
  y1 = y + h;
  if (x1 > fb->w) x1 = fb->w;
  if (y1 > fb->h) y1 = fb->h;
  for (yy = y0; yy < y1; yy++) {
    mote_u32 *row = fb->px + (size_t)yy * (size_t)fb->w;
    for (xx = x0; xx < x1; xx++) row[xx] = rgb;
  }
}

static const unsigned char *glyph_bits(mote_u32 cp) {
  int lo, hi;
  if (cp >= 32 && cp <= 126)
    return soft_font_bits[cp - 32];
  lo = 0;
  hi = SOFT_FONT_CYR_N - 1;
  while (lo <= hi) {
    int mid = lo + (hi - lo) / 2;
    mote_u32 v = soft_font_cyr_cp[mid];
    if (v == cp) return soft_font_cyr_bits[mid];
    if (v < cp) lo = mid + 1;
    else hi = mid - 1;
  }
  return NULL;
}

static void put_glyph_bits(SoftFb *fb, int x, int y, const unsigned char *g,
                           mote_u32 rgb) {
  int sc, r, c, sy, sx;
  if (!g) return;
  sc = fb->scale > 0 ? fb->scale : 1;
  for (r = 0; r < SOFT_FONT_H; r++) {
    unsigned char bits = g[r];
    for (c = 0; c < SOFT_FONT_W; c++) {
      if (!(bits & (0x80 >> c))) continue;
      for (sy = 0; sy < sc; sy++) {
        int py = y + r * sc + sy;
        if (py < 0 || py >= fb->h) continue;
        for (sx = 0; sx < sc; sx++) {
          int px = x + c * sc + sx;
          if (px < 0 || px >= fb->w) continue;
          fb->px[(size_t)py * (size_t)fb->w + (size_t)px] = rgb;
        }
      }
    }
  }
}

static void put_glyph(SoftFb *fb, int x, int y, mote_u32 cp, mote_u32 rgb) {
  const unsigned char *g = glyph_bits(cp);
  if (!g) g = soft_font_bits['?' - 32];
  put_glyph_bits(fb, x, y, g, rgb);
}

void soft_draw_text(SoftFb *fb, int x, int y, const char *s, int n, mote_u32 rgb) {
  int i = 0, cx = x;
  int adv = soft_font_w(fb);
  if (!fb->px || !s || n <= 0) return;
  while (i < n) {
    mote_u32 cp;
    int len = utf8_decode(s + i, (size_t)(n - i), &cp);
    if (len <= 0) break;
    i += len;
    if (cp == 0x2013 || cp == 0x2014 || cp == 0x2212)
      cp = (mote_u32)'-';
    else if (cp == 0x00A0)
      cp = (mote_u32)' ';
    else if (cp == 0x2026)
      cp = (mote_u32)'.';
    else if (cp == 0x00B7 || cp == 0x2022 || cp == 0x2219)
      cp = (mote_u32)'.'; /* · • ∙ — whitespace markers */
    else if (cp == 0x00BB || cp == 0x203A)
      cp = (mote_u32)'>'; /* » › — tab markers */
    put_glyph(fb, cx, y, cp, rgb);
    cx += adv;
  }
}

void soft_blit_caret(SoftFb *fb) {
  int x, y, h, yy;
  if (!fb->caret_on || !fb->px) return;
  x = fb->caret_x;
  y = fb->caret_y;
  h = fb->caret_h > 0 ? fb->caret_h : soft_font_h(fb);
  if (x < 0 || x >= fb->w) return;
  for (yy = 0; yy < h; yy++) {
    int py = y + yy;
    if (py < 0 || py >= fb->h) continue;
    fb->px[(size_t)py * (size_t)fb->w + (size_t)x] ^= 0x00FFFFFF;
  }
}

void soft_dump_once(const SoftFb *fb) {
  static int dumped;
  const char *path = getenv("MOTE_DUMP_FB");
  FILE *f;
  size_t i, n = (size_t)fb->w * (size_t)fb->h;
  if (dumped || !path || !fb->px) return;
  dumped = 1;
  f = fopen(path, "wb");
  if (!f) return;
  fprintf(f, "P6\n%d %d\n255\n", fb->w, fb->h);
  for (i = 0; i < n; i++) {
    unsigned char rgb[3];
    rgb[0] = (unsigned char)(fb->px[i] >> 16);
    rgb[1] = (unsigned char)(fb->px[i] >> 8);
    rgb[2] = (unsigned char)fb->px[i];
    fwrite(rgb, 1, 3, f);
  }
  fclose(f);
}
