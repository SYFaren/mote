/* mote — stubs so core unit tests link without a GUI overlay */
#define _POSIX_C_SOURCE 200809L
#include "platform.h"
#include <stdio.h>
#include <string.h>
#include <unistd.h>

FILE *plat_fopen(const char *path, const char *mode) {
  return fopen(path, mode);
}
int plat_remove(const char *path) { return remove(path); }
int plat_rename(const char *from, const char *to) { return rename(from, to); }
void plat_fsync_file(FILE *f) {
  int fd;
  if (!f) return;
  fflush(f);
  fd = fileno(f);
  if (fd >= 0) (void)fsync(fd);
}
int plat_config_path(char *out, size_t n) {
  if (snprintf(out, n, "/tmp/mote-test-config") >= (int)n) return -1;
  return 0;
}

Plat *plat_create(const char *t, int w, int h) {
  (void)t; (void)w; (void)h; return NULL;
}
void plat_destroy(Plat *p) { (void)p; }
void plat_wait(Plat *p) { (void)p; }
mote_bool plat_poll(Plat *p, PlatEvent *ev) { (void)p; (void)ev; return MOTE_FALSE; }
void plat_get_size(Plat *p, int *w, int *h) {
  (void)p; if (w) *w = 0; if (h) *h = 0;
}
int plat_font_w(Plat *p) { (void)p; return 8; }
int plat_font_h(Plat *p) { (void)p; return 16; }
void plat_set_font_px(Plat *p, int px) { (void)p; (void)px; }
int plat_font_px(Plat *p) { (void)p; return 15; }
void plat_begin_frame(Plat *p) { (void)p; }
void plat_clear(Plat *p, mote_u32 rgb) { (void)p; (void)rgb; }
void plat_fill_rect(Plat *p, int x, int y, int w, int h, mote_u32 rgb) {
  (void)p; (void)x; (void)y; (void)w; (void)h; (void)rgb;
}
void plat_draw_text(Plat *p, int x, int y, const char *s, int n, mote_u32 rgb) {
  (void)p; (void)x; (void)y; (void)s; (void)n; (void)rgb;
}
void plat_end_frame(Plat *p) { (void)p; }
void plat_set_title(Plat *p, const char *title) { (void)p; (void)title; }
mote_bool plat_set_caret(Plat *p, int x, int y, int h, mote_bool on) {
  (void)p; (void)x; (void)y; (void)h; (void)on; return MOTE_FALSE;
}
char *plat_clipboard_get(Plat *p, size_t *out_len) {
  (void)p; if (out_len) *out_len = 0; return NULL;
}
mote_bool plat_clipboard_set(Plat *p, const char *s, size_t n) {
  (void)p; (void)s; (void)n; return MOTE_FALSE;
}
