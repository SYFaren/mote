#ifndef MOTE_APP_H
#define MOTE_APP_H

#include "editor.h"

typedef struct {
  const char *tag;       /* --version suffix like " (console)" */
  const char *geom_help; /* -g line in --help */
  const char *notes;     /* last --help lines: config path, env vars */
  const char *no_plat;   /* error when plat_create fails */
  int min_w, min_h;      /* accepted -g range; max 0 = no upper bound */
  int max_w, max_h;
  /* -g is not a GUI window size (text cells, or a framebuffer area): the
     config's win_w/win_h are neither passed to plat_create nor overwritten
     on exit. */
  mote_bool cell_geom;
  void (*after_first_draw)(Editor *e); /* may be NULL */
} MoteApp;

/* Parse args, load config, open files, run the event loop, save config. */
int mote_main(int argc, char **argv, const MoteApp *app);

#endif
