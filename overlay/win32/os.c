/* mote overlay/win32 — OS file + config path (UTF-8 paths) */
#include "platform.h"
#include "common.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <direct.h>

static wchar_t *utf8_to_wide(const char *s) {
  int n;
  wchar_t *w;
  if (!s) return NULL;
  n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  if (n <= 0) return NULL;
  w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
  if (!w) return NULL;
  if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n)) {
    free(w);
    return NULL;
  }
  return w;
}

FILE *plat_fopen(const char *path, const char *mode) {
  wchar_t *wp, wm[8];
  FILE *f;
  int i;
  if (!path || !mode) return NULL;
  wp = utf8_to_wide(path);
  if (!wp) return NULL;
  for (i = 0; mode[i] && i < 7; i++) wm[i] = (wchar_t)(unsigned char)mode[i];
  wm[i] = 0;
  f = _wfopen(wp, wm);
  free(wp);
  return f;
}

int plat_remove(const char *path) {
  wchar_t *w = utf8_to_wide(path);
  BOOL ok;
  if (!w) return -1;
  ok = DeleteFileW(w);
  free(w);
  return ok ? 0 : -1;
}

int plat_rename(const char *from, const char *to) {
  wchar_t *wfrom = utf8_to_wide(from);
  wchar_t *wto = utf8_to_wide(to);
  BOOL ok;
  if (!wfrom || !wto) {
    free(wfrom);
    free(wto);
    return -1;
  }
  ok = MoveFileExW(wfrom, wto, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
  free(wfrom);
  free(wto);
  return ok ? 0 : -1;
}

void plat_fsync_file(FILE *f) {
  int fd;
  if (!f) return;
  fflush(f);
  fd = _fileno(f);
  if (fd >= 0) (void)_commit(fd);
}

int plat_config_path(char *out, size_t n) {
  const char *home;
  char dir[512];
  home = getenv("APPDATA");
  if (!home || !home[0]) return -1;
  if (snprintf(dir, sizeof dir, "%s\\" MOTE_NAME, home) >= (int)sizeof dir)
    return -1;
  _mkdir(dir);
  if (snprintf(out, n, "%s\\config", dir) >= (int)n) return -1;
  return 0;
}
