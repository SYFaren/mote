/* mote/mote-x — developer: SYFaren */
#ifndef MOTE_THEME_H
#define MOTE_THEME_H
#include <stdint.h>

typedef struct {
  const char *name;
  uint32_t bg, fg, sel, line;
  uint32_t gutter_bg, gutter_fg;
  uint32_t status, status_fg, caret;
  uint32_t help_bg, help_bd;
  uint32_t kw, type, str, comment, number, preproc, match, bracket;
} Theme;

int theme_count(void);
const Theme *theme_get(int id);
const char *theme_name(int id);

#endif
