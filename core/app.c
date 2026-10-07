#include "app.h"
#include "common.h"
#include "config.h"
#include "theme.h"
#include "mote_snprintf.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Only fputs here: pulling in printf or sscanf bloats the DOS build. */
static void usage(const MoteApp *app) {
  char line[96];
  fputs("usage: " MOTE_NAME " [-h|-v|-H|-g WxH] [file ...]\n"
        "  -h, --help         show this help\n"
        "  -v, --version      print version\n"
        "  -H, --start-help   open the key help on start\n",
        stderr);
  mote_snprintf(line, sizeof line, "  -g, --geometry     %s\n", app->geom_help);
  fputs(line, stderr);
  mote_snprintf(line, sizeof line, "  file ...           open up to %d files\n", MAX_DOCS);
  fputs(line, stderr);
  fputs(app->notes, stderr);
}

static void version(const MoteApp *app) {
  char line[128];
  mote_snprintf(line, sizeof line, "%s %s+%s - %s%s\n", MOTE_NAME, MOTE_VERSION,
                MOTE_BUILD, MOTE_AUTHOR, app->tag);
  fputs(line, stdout);
}

static void fail(const char *a, const char *b) {
  fputs(MOTE_NAME ": ", stderr);
  fputs(a, stderr);
  fputs(b, stderr);
  fputs("\n", stderr);
}

static const char *parse_int(const char *s, int *out) {
  int v = 0;
  if (*s < '0' || *s > '9') return NULL;
  while (*s >= '0' && *s <= '9' && v < 100000) v = v * 10 + (*s++ - '0');
  *out = v;
  return s;
}

/* "WxH" (also "WXH" or "W,H") within the overlay's -g range. */
static mote_bool parse_geom(const MoteApp *app, const char *s, int *w, int *h) {
  int a, b;
  if (!(s = parse_int(s, &a))) return MOTE_FALSE;
  if (*s != 'x' && *s != 'X' && *s != ',') return MOTE_FALSE;
  if (!(s = parse_int(s + 1, &b)) || *s) return MOTE_FALSE;
  if (a < app->min_w || b < app->min_h) return MOTE_FALSE;
  if ((app->max_w && a > app->max_w) || (app->max_h && b > app->max_h)) return MOTE_FALSE;
  *w = a;
  *h = b;
  return MOTE_TRUE;
}

static mote_bool is_opt(const char *arg, const char *s, const char *l) {
  return strcmp(arg, s) == 0 || strcmp(arg, l) == 0;
}

static void cfg_to_editor(const MoteCfg *cfg, Editor *e) {
  int i;
  e->theme_id = cfg->theme_id < theme_count() ? cfg->theme_id : 0;
  e->wrap = cfg->wrap != 0;
  e->show_ws = cfg->show_ws != 0;
  e->find_case = cfg->find_case != 0;
  e->find_word = cfg->find_word != 0;
  e->nrecent = cfg->nrecent < MAX_RECENT ? cfg->nrecent : MAX_RECENT;
  for (i = 0; i < e->nrecent; i++)
    mote_snprintf(e->recent[i], sizeof e->recent[0], "%s", cfg->recent[i]);
}

static void editor_to_cfg(const Editor *e, MoteCfg *cfg) {
  int i;
  cfg->theme_id = e->theme_id;
  cfg->wrap = e->wrap;
  cfg->show_ws = e->show_ws;
  cfg->find_case = e->find_case;
  cfg->find_word = e->find_word;
  cfg->nrecent = e->nrecent < MOTE_CFG_RECENT ? e->nrecent : MOTE_CFG_RECENT;
  for (i = 0; i < cfg->nrecent; i++)
    mote_snprintf(cfg->recent[i], sizeof cfg->recent[0], "%s", e->recent[i]);
}

int mote_main(int argc, char **argv, const MoteApp *app) {
  static Editor ed; /* about 100 KB, too much for the wasm and DOS stacks */
  Plat *plat;
  MoteCfg cfg;
  const char *files[MAX_DOCS];
  int nfiles = 0, i, w, h;
  mote_bool start_help = getenv("MOTE_START_HELP") != NULL;

  cfg_load(&cfg);
  w = app->cell_geom ? 0 : cfg.win_w;
  h = app->cell_geom ? 0 : cfg.win_h;

  for (i = 1; i < argc; i++) {
    const char *a = argv[i];
    if (is_opt(a, "-h", "--help")) {
      usage(app);
      return 0;
    }
    if (is_opt(a, "-v", "--version")) {
      version(app);
      return 0;
    }
    if (is_opt(a, "-g", "--geometry")) {
      if (i + 1 >= argc || !parse_geom(app, argv[++i], &w, &h)) {
        fail("bad -g, expected ", app->geom_help);
        return 1;
      }
    } else if (is_opt(a, "-H", "--start-help")) {
      start_help = MOTE_TRUE;
    } else if (a[0] == '-') {
      fail("unknown option ", a);
      usage(app);
      return 1;
    } else if (nfiles < MAX_DOCS) {
      files[nfiles++] = a;
    }
  }

  plat = plat_create(MOTE_NAME, w, h);
  if (!plat) {
    fail(app->no_plat, "");
    return 1;
  }
  if (!ed_init(&ed)) {
    plat_destroy(plat);
    return 1;
  }
  cfg_to_editor(&cfg, &ed);
  plat_set_font_px(plat, cfg.font_px);

  for (i = 0; i < nfiles; i++) {
    if (i > 0) ed_new_doc(&ed);
    ed_open_path(&ed, files[i]);
  }
  ed.cur = 0;
  if (start_help) ed.mode = MODE_HELP; /* after files, so help shows over them */

  ed_draw(&ed, plat);
  if (app->after_first_draw) app->after_first_draw(&ed);
  while (!ed.want_quit) {
    PlatEvent ev;
    plat_wait(plat);
    while (!ed.want_quit && plat_poll(plat, &ev)) ed_handle(&ed, plat, &ev);
    if (ed.need_draw) ed_draw(&ed, plat);
  }

  if (!app->cell_geom) plat_get_size(plat, &cfg.win_w, &cfg.win_h);
  cfg.font_px = plat_font_px(plat);
  editor_to_cfg(&ed, &cfg);
  cfg_save(&cfg);

  ed_free(&ed);
  plat_destroy(plat);
  return 0;
}
