#define _POSIX_C_SOURCE 200112L
/* mote — console overlay entry */
#include "editor.h"
#include "common.h"
#include "config.h"
#include "theme.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void usage(void) {
  fprintf(stderr,
          "usage: %s [-h|-v|-H|-g COLSxROWS] [file ...]\n"
          "TTY truecolor; Ctrl+Q quit, F1 help; ~/.config/%s/config\n",
          MOTE_NAME, MOTE_NAME);
}

static int parse_geom(const char *s, int *w, int *h) {
  int a = 0, b = 0;
  char sep = 0;
  if (sscanf(s, "%d%c%d", &a, &sep, &b) != 3) return -1;
  if (sep != 'x' && sep != 'X' && sep != ',') return -1;
  if (a < 40 || b < 10) return -1;
  if (a > 512 || b > 256) return -1;
  *w = a;
  *h = b;
  return 0;
}

int main(int argc, char **argv) {
  Plat *plat;
  Editor ed;
  MoteCfg cfg;
  const char *files[MAX_DOCS];
  int nfiles = 0, i, start_help = 0;

  cfg_load(&cfg);
  /* console: geometry is cells; keep a sane default if GUI size was saved */
  if (cfg.win_w > 512 || cfg.win_h > 256 || cfg.win_w < 40 || cfg.win_h < 10) {
    cfg.win_w = 0;
    cfg.win_h = 0;
  }

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage();
      return 0;
    }
    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
      printf("%s %s — %s (console)\n", MOTE_NAME, MOTE_VERSION, MOTE_AUTHOR);
      return 0;
    }
    if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--geometry") == 0) {
      if (i + 1 >= argc || parse_geom(argv[++i], &cfg.win_w, &cfg.win_h) != 0) {
        fprintf(stderr, "%s: bad -g, use COLSxROWS\n", MOTE_NAME);
        return 1;
      }
      continue;
    }
    if (strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "--start-help") == 0) {
      start_help = 1;
      continue;
    }
    if (argv[i][0] == '-') {
      fprintf(stderr, "%s: unknown option %s\n", MOTE_NAME, argv[i]);
      usage();
      return 1;
    }
    if (nfiles < MAX_DOCS) files[nfiles++] = argv[i];
  }

  plat = plat_create(MOTE_NAME, cfg.win_w, cfg.win_h);
  if (!plat) {
    fprintf(stderr, "%s: need a TTY (stdin/stdout)\n", MOTE_NAME);
    return 1;
  }
  if (!ed_init(&ed)) {
    plat_destroy(plat);
    return 1;
  }

  if (cfg.theme_id >= theme_count()) cfg.theme_id = 0;
  ed.theme_id = cfg.theme_id;
  ed.wrap = cfg.wrap != 0;
  ed.show_ws = cfg.show_ws != 0;
  ed.find_case = cfg.find_case != 0;
  ed.find_word = cfg.find_word != 0;
  ed.nrecent = cfg.nrecent;
  if (ed.nrecent > MAX_RECENT) ed.nrecent = MAX_RECENT;
  for (i = 0; i < ed.nrecent; i++)
    snprintf(ed.recent[i], sizeof ed.recent[0], "%s", cfg.recent[i]);
  plat_set_font_px(plat, cfg.font_px);

  for (i = 0; i < nfiles; i++) {
    if (i > 0) ed_new_doc(&ed);
    ed_open_path(&ed, files[i]);
  }
  if (nfiles > 0) ed.cur = 0;

  if (start_help || getenv("MOTE_START_HELP")) {
    if (start_help) {
      static char env[] = "MOTE_START_HELP=1";
      putenv(env);
    }
    ed.mode = MODE_HELP;
    ed.need_draw = MOTE_TRUE;
  }

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
  cfg.theme_id = ed.theme_id;
  cfg.font_px = plat_font_px(plat);
  cfg.wrap = ed.wrap;
  cfg.show_ws = ed.show_ws;
  cfg.find_case = ed.find_case;
  cfg.find_word = ed.find_word;
  cfg.nrecent = ed.nrecent;
  if (cfg.nrecent > MOTE_CFG_RECENT) cfg.nrecent = MOTE_CFG_RECENT;
  for (i = 0; i < cfg.nrecent; i++)
    snprintf(cfg.recent[i], sizeof cfg.recent[0], "%s", ed.recent[i]);
  cfg_save(&cfg);

  ed_free(&ed);
  plat_destroy(plat);
  return 0;
}
