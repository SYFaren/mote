/* mote core — config.c */
#include "config.h"
#include "common.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mote_snprintf.h"

void cfg_defaults(MoteCfg *c) {
  memset(c, 0, sizeof *c);
  c->win_w = 800;
  c->win_h = 600;
  c->theme_id = 0;
  c->font_px = 15;
}

static void clamp_wh(MoteCfg *c) {
  if (c->win_w < 200) c->win_w = 200;
  if (c->win_h < 120) c->win_h = 120;
  if (c->win_w > 8192) c->win_w = 8192;
  if (c->win_h > 8192) c->win_h = 8192;
  if (c->font_px < 8) c->font_px = 8;
  if (c->font_px > 48) c->font_px = 48;
  if (c->theme_id < 0) c->theme_id = 0;
}

int cfg_load(MoteCfg *c) {
  char path[640], line[1100];
  FILE *f;
  cfg_defaults(c);
  if (plat_config_path(path, sizeof path) != 0) return 0;
  f = plat_fopen(path, "r");
  if (!f) return 0;
  while (fgets(line, sizeof line, f)) {
    char *eq = strchr(line, '=');
    char *nl;
    if (!eq) continue;
    *eq = 0;
    nl = strchr(eq + 1, '\n');
    if (nl) *nl = 0;
    if (strcmp(line, "win_w") == 0) c->win_w = atoi(eq + 1);
    else if (strcmp(line, "win_h") == 0) c->win_h = atoi(eq + 1);
    else if (strcmp(line, "theme") == 0) c->theme_id = atoi(eq + 1);
    else if (strcmp(line, "font_px") == 0) c->font_px = atoi(eq + 1);
    else if (strcmp(line, "wrap") == 0) c->wrap = atoi(eq + 1) != 0;
    else if (strcmp(line, "show_ws") == 0) c->show_ws = atoi(eq + 1) != 0;
    else if (strcmp(line, "find_case") == 0) c->find_case = atoi(eq + 1) != 0;
    else if (strcmp(line, "find_word") == 0) c->find_word = atoi(eq + 1) != 0;
    else if (strcmp(line, "recent") == 0 && c->nrecent < MOTE_CFG_RECENT) {
      mote_snprintf(c->recent[c->nrecent], sizeof c->recent[0], "%s", eq + 1);
      if (c->recent[c->nrecent][0]) c->nrecent++;
    }
  }
  fclose(f);
  clamp_wh(c);
  return 0;
}

int cfg_save(const MoteCfg *c) {
  char path[640], line[1200];
  FILE *f;
  int i;
  if (plat_config_path(path, sizeof path) != 0) return -1;
  f = plat_fopen(path, "w");
  if (!f) return -1;
  mote_snprintf(line, sizeof line,
                "win_w=%d\nwin_h=%d\ntheme=%d\nfont_px=%d\n"
                "wrap=%d\nshow_ws=%d\nfind_case=%d\nfind_word=%d\n",
                c->win_w, c->win_h, c->theme_id, c->font_px, c->wrap, c->show_ws,
                c->find_case, c->find_word);
  fputs(line, f);
  for (i = 0; i < c->nrecent; i++) {
    if (!c->recent[i][0]) continue;
    mote_snprintf(line, sizeof line, "recent=%s\n", c->recent[i]);
    fputs(line, f);
  }
  fclose(f);
  return 0;
}
