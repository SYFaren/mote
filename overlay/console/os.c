#define _POSIX_C_SOURCE 200809L
#include "platform.h"
#include "common.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/file.h>
#include <fcntl.h>

FILE *plat_fopen(const char *path, const char *mode) {
  if (!path || !mode) return NULL;
  return fopen(path, mode);
}

FILE *plat_lock_file(const char *path) {
  int fd = open(path, O_RDWR | O_CREAT | O_NOFOLLOW, 0600);
  FILE *f;
  if (fd < 0) return NULL;
  if (flock(fd, LOCK_EX | LOCK_NB) != 0) {
    close(fd);
    return NULL;
  }
  f = fdopen(fd, "r+");
  if (!f) close(fd);
  return f;
}

FILE *plat_private_write(const char *path) {
  int fd = open(path, O_WRONLY | O_CREAT | O_TRUNC | O_NOFOLLOW, 0600);
  FILE *f;
  if (fd < 0) return NULL;
  if (fchmod(fd, 0600) != 0) { close(fd); return NULL; }
  f = fdopen(fd, "wb");
  if (!f) close(fd);
  return f;
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
