/* mote/mote/linux — developer: SYFaren */
#include "config.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#ifdef _WIN32
#include <direct.h>
#define mote_mkdir(p) _mkdir(p)
#else
#define mote_mkdir(p) mkdir((p), 0755)
#endif

void cfg_defaults(MoteCfg *c) {
  c->win_w = 800;
  c->win_h = 600;
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

int cfg_load(MoteCfg *c) {
  char path[640], line[256];
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
  }
  fclose(f);
  if (c->win_w < 200) c->win_w = 200;
  if (c->win_h < 120) c->win_h = 120;
  if (c->win_w > 8192) c->win_w = 8192;
  if (c->win_h > 8192) c->win_h = 8192;
  return 0;
}

int cfg_save(const MoteCfg *c) {
  char path[640];
  FILE *f;
  if (cfg_path(path, sizeof path) != 0) return -1;
  f = fopen(path, "w");
  if (!f) return -1;
  fprintf(f, "win_w=%d\nwin_h=%d\n", c->win_w, c->win_h);
  fclose(f);
  return 0;
}
