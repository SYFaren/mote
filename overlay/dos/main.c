/* mote — DOS (DJGPP) overlay entry */
#include "editor.h"
#include "common.h"
#include "config.h"
#include "theme.h"
#include "mote_snprintf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void die(const char *msg) {
  fputs(msg, stderr);
  fputc('\n', stderr);
}

static void usage(void) {
  die("usage: mote [-h|-v|-H|-g COLSxROWS] [file ...]");
  die("  -h, --help         show this help");
  die("  -v, --version      print version");
  die("  -H, --start-help   open help overlay on start");
  die("  -g, --geometry     text size COLSxROWS (40..132 x 10..50)");
  die("  file ...           open up to 6 files");
  die("DOS VGA text; Ctrl+Q quit, F1 help; config MOTE\\CONFIG");
  die("env: MOTE_START_HELP=1  MOTE_KEYTRACE=1 (writes KEYTRACE.LOG)");
}

static int parse_int(const char *s, int *out) {
  int v = 0, any = 0;
  if (!s) return -1;
  while (*s >= '0' && *s <= '9') {
    v = v * 10 + (*s - '0');
    s++;
    any = 1;
  }
  if (!any) return -1;
  *out = v;
  return 0;
}

static int parse_geom(const char *s, int *w, int *h) {
  int a = 0, b = 0;
  const char *p;
  if (parse_int(s, &a) != 0) return -1;
  p = s;
  while (*p >= '0' && *p <= '9') p++;
  if (*p != 'x' && *p != 'X' && *p != ',') return -1;
  p++;
  if (parse_int(p, &b) != 0) return -1;
  if (a < 40 || b < 10 || a > 132 || b > 50) return -1;
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
  char msg[160];

  cfg_load(&cfg);
  if (cfg.win_w > 132 || cfg.win_h > 50 || cfg.win_w < 40 || cfg.win_h < 10) {
    cfg.win_w = 80;
    cfg.win_h = 25;
  }

  for (i = 1; i < argc; i++) {
    if (strcmp(argv[i], "-h") == 0 || strcmp(argv[i], "--help") == 0) {
      usage();
      return 0;
    }
    if (strcmp(argv[i], "-v") == 0 || strcmp(argv[i], "--version") == 0) {
      mote_snprintf(msg, sizeof msg, "%s %s - %s (dos)", MOTE_NAME, MOTE_VERSION,
                    MOTE_AUTHOR);
      die(msg);
      return 0;
    }
    if (strcmp(argv[i], "-g") == 0 || strcmp(argv[i], "--geometry") == 0) {
      if (i + 1 >= argc || parse_geom(argv[++i], &cfg.win_w, &cfg.win_h) != 0) {
        die("mote: bad -g, use COLSxROWS");
        return 1;
      }
      continue;
    }
    if (strcmp(argv[i], "-H") == 0 || strcmp(argv[i], "--start-help") == 0) {
      start_help = 1;
      continue;
    }
    if (argv[i][0] == '-') {
      die("mote: unknown option");
      usage();
      return 1;
    }
    if (nfiles < MAX_DOCS) files[nfiles++] = argv[i];
  }

  plat = plat_create(MOTE_NAME, cfg.win_w, cfg.win_h);
  if (!plat) {
    die("mote: cannot init VGA text");
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
    mote_snprintf(ed.recent[i], sizeof ed.recent[0], "%s", cfg.recent[i]);
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
    mote_snprintf(cfg.recent[i], sizeof cfg.recent[0], "%s", ed.recent[i]);
  cfg_save(&cfg);

  ed_free(&ed);
  plat_destroy(plat);
  return 0;
}
