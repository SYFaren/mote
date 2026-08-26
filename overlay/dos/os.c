/* mote overlay/dos — file + config (DJGPP) */
#include "platform.h"
#include "common.h"
#include "mote_snprintf.h"
#include <stdio.h>
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

/* Config: MOTE\CONFIG under cwd */
int plat_config_path(char *out, size_t n) {
  mkdir("MOTE", 0755);
  if (mote_snprintf(out, n, "MOTE/CONFIG") >= (int)n) return -1;
  return 0;
}
