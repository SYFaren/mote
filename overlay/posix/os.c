/* mote overlay/x11 — OS file + config path */
#define _POSIX_C_SOURCE 200809L
#include "platform.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>

FILE *plat_fopen(const char *path, const char *mode) {
  if (!path || !mode) return NULL;
  return fopen(path, mode);
}

int plat_remove(const char *path) {
  if (!path) return -1;
  return remove(path);
}

int plat_rename(const char *from, const char *to) {
  if (!from || !to) return -1;
  return rename(from, to);
}

void plat_fsync_file(FILE *f) {
  int fd;
  if (!f) return;
  fflush(f);
  fd = fileno(f);
  if (fd >= 0) (void)fsync(fd);
}

int plat_config_path(char *out, size_t n) {
  const char *home;
  char dir[512];
  char *slash;

  home = getenv("XDG_CONFIG_HOME");
  if (home && home[0]) {
    if (snprintf(dir, sizeof dir, "%s/" MOTE_NAME, home) >= (int)sizeof dir)
      return -1;
  } else {
    home = getenv("HOME");
    if (!home || !home[0]) return -1;
    if (snprintf(dir, sizeof dir, "%s/.config/" MOTE_NAME, home) >=
        (int)sizeof dir)
      return -1;
  }
  slash = strrchr(dir, '/');
  if (slash && slash != dir) {
    *slash = 0;
    mkdir(dir, 0755);
    *slash = '/';
  }
  mkdir(dir, 0755);
  if (snprintf(out, n, "%s/config", dir) >= (int)n) return -1;
  return 0;
}
