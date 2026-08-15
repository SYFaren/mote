/* mote/mote/windows — developer: SYFaren */
#include "editor.h"
#include "common.h"
#include "config.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
  fprintf(stderr,
          "usage: %s [options] [file]\n"
          "  -h, --help          show help\n"
          "  -v, --version       show version\n"
          "  -g WxH              window size (e.g. -g 1000x700)\n"
          "Config: %%APPDATA%%\\%s\\config  (window size)\n",
          MOTE_NAME, MOTE_NAME);
}

static int parse_geom(const char *s, int *w, int *h) {
  int a = 0, b = 0;
  char sep = 0;
  if (sscanf(s, "%d%c%d", &a, &sep, &b) != 3) return -1;
  if (sep != 'x' && sep != 'X' && sep != ',') return -1;
  if (a < 200 || b < 120) return -1;
  *w = a;
  *h = b;
  return 0;
}

int main(int argc, char **argv) {
  Plat *plat;
  Editor ed;
  MoteCfg cfg;
  const char *file = NULL;
  int i, geom_set = 0;

  cfg_load(&cfg);

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage();
      return 0;
    }
    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
      printf("%s %s — %s\n", MOTE_NAME, MOTE_VERSION, MOTE_AUTHOR);
      return 0;
    }
    if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--geometry") == 0) {
      if (i + 1 >= argc || parse_geom(argv[++i], &cfg.win_w, &cfg.win_h) != 0) {
        fprintf(stderr, "%s: bad -g, use WxH\n", MOTE_NAME);
        return 1;
      }
      geom_set = 1;
      (void)geom_set;
      continue;
    }
    if (argv[i][0] == '-') {
      fprintf(stderr, "%s: unknown option %s\n", MOTE_NAME, argv[i]);
      usage();
      return 1;
    }
    if (!file) file = argv[i];
  }

  plat = plat_create(MOTE_NAME, cfg.win_w, cfg.win_h);
  if (!plat) {
    fprintf(stderr, "%s: cannot open window\n", MOTE_NAME);
    return 1;
  }
  if (!ed_init(&ed)) {
    plat_destroy(plat);
    return 1;
  }
  if (file) ed_open_path(&ed, file);

  ed_draw(&ed, plat);
  while (!ed.want_quit) {
    PlatEvent ev;
    plat_wait(plat);
    while (plat_poll(plat, &ev)) {
      ed_handle(&ed, plat, &ev);
      if (ed.want_quit) break;
    }
    if (ed.need_draw) ed_draw(&ed, plat);
  }

  plat_get_size(plat, &cfg.win_w, &cfg.win_h);
  cfg_save(&cfg);

  ed_free(&ed);
  plat_destroy(plat);
  return 0;
}
