/* mote core — theme.h */
#ifndef MOTE_THEME_H
#define MOTE_THEME_H

#include "mote_ansi.h"

typedef struct {
  const char *name;
  mote_u32 bg, fg, sel, line;
  mote_u32 gutter_bg, gutter_fg;
  mote_u32 status, status_fg, caret;
  mote_u32 help_bg, help_bd;
  mote_u32 kw, type, str, comment, number, preproc, match, bracket;
} Theme;

int theme_count(void);
const Theme *theme_get(int id);
const char *theme_name(int id);

#endif
