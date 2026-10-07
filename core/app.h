/* mote core — app.h: program entry shared by every overlay */
#ifndef MOTE_APP_H
#define MOTE_APP_H

#include "editor.h"

typedef struct {
  const char *tag;       /* --version suffix, e.g. " (console)" */
  const char *geom_help; /* -g line in --help */
  const char *notes;     /* last --help lines: config path, env vars */
  const char *no_plat;   /* error when plat_create fails */
  int min_w, min_h;      /* accepted -g range; max 0 = no upper bound */
  int max_w, max_h;
  /* -g is in text cells: the config's win_w/win_h are GUI window pixels,
     so they are neither passed to plat_create nor overwritten on exit. */
  mote_bool cell_geom;
  void (*after_first_draw)(Editor *e); /* optional */
} MoteApp;

/* Parse args, load config, open files, run the event loop, save config. */
int mote_main(int argc, char **argv, const MoteApp *app);

#endif
