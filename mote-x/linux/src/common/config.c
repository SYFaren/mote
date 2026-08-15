/* mote-x — developer: SYFaren */
#include "config.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <direct.h>
#define mote_mkdir(p) _mkdir(p)
#else
#include <sys/stat.h>
#define mote_mkdir(p) mkdir((p), 0755)
#endif

void cfg_defaults(MoteCfg *c) {
  memset(c, 0, sizeof *c);
  c->win_w = 800;
  c->win_h = 600;
  c->theme_id = 0;
  c->font_px = 15;
}

static int cfg_dir(char *out, size_t n) {
  const char *home;
#ifdef _WIN32
  home = getenv("APPDATA");
  if (!home || !home[0]) return -1;
  if (snprintf(out, n, "%s\\" MOTE_NAME, home) >= (int)n) return -1;
#else
  home = getenv("XDG_CONFIG_HOME");
  if (home && home[0]) {
    if (snprintf(out, n, "%s/" MOTE_NAME, home) >= (int)n) return -1;
  } else {
    home = getenv("HOME");
    if (!home || !home[0]) return -1;
    if (snprintf(out, n, "%s/.config/" MOTE_NAME, home) >= (int)n) return -1;
  }
#endif
  return 0;
}

static int cfg_path(char *out, size_t n) {
  char dir[512];
  if (cfg_dir(dir, sizeof dir) != 0) return -1;
#ifndef _WIN32
  {
    char *slash = strrchr(dir, '/');
    if (slash && slash != dir) {
      *slash = 0;
      mote_mkdir(dir);
      *slash = '/';
    }
  }
#endif
  mote_mkdir(dir);
  if (snprintf(out, n, "%s%cconfig", dir,
#ifdef _WIN32
               '\\'
#else
               '/'
#endif
               ) >= (int)n)
    return -1;
  return 0;
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
  if (cfg_path(path, sizeof path) != 0) return 0;
  f = fopen(path, "r");
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
      snprintf(c->recent[c->nrecent], sizeof c->recent[0], "%s", eq + 1);
      if (c->recent[c->nrecent][0]) c->nrecent++;
    }
  }
  fclose(f);
  clamp_wh(c);
  return 0;
}

int cfg_save(const MoteCfg *c) {
  char path[640];
  FILE *f;
  int i;
  if (cfg_path(path, sizeof path) != 0) return -1;
  f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f,
          "win_w=%d\nwin_h=%d\ntheme=%d\nfont_px=%d\n"
          "wrap=%d\nshow_ws=%d\nfind_case=%d\nfind_word=%d\n",
          c->win_w, c->win_h, c->theme_id, c->font_px, c->wrap, c->show_ws,
          c->find_case, c->find_word);
  for (i = 0; i < c->nrecent; i++)
    if (c->recent[i][0]) fprintf(f, "recent=%s\n", c->recent[i]);
  fclose(f);
  return 0;
}
