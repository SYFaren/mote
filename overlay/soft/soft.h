/* Shared RGB soft-framebuffer + bitmap font for GUI overlays. */
#ifndef MOTE_SOFT_H
#define MOTE_SOFT_H

#include "mote_ansi.h"
#include <stddef.h>

typedef struct SoftFb {
  mote_u32 *px;
  int w, h;
  int font_px, scale;
  int caret_x, caret_y, caret_h;
  mote_bool caret_on;
} SoftFb;

mote_bool soft_resize(SoftFb *fb, int w, int h);
void soft_free(SoftFb *fb);
void soft_set_font_px(SoftFb *fb, int px);
int soft_font_w(const SoftFb *fb);
int soft_font_h(const SoftFb *fb);
void soft_clear(SoftFb *fb, mote_u32 rgb);
void soft_fill_rect(SoftFb *fb, int x, int y, int w, int h, mote_u32 rgb);
void soft_draw_text(SoftFb *fb, int x, int y, const char *s, int n, mote_u32 rgb);
void soft_blit_caret(SoftFb *fb);

#endif
