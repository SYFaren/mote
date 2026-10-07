#include "platform.h"
#include "common.h"
#include "mote_snprintf.h"
#include "winutil.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <direct.h>

FILE *plat_fopen(const char *path, const char *mode) {
  wchar_t *wp, wm[8];
  FILE *f;
  int i;
  if (!path || !mode) return NULL;
  wp = win_wide(path);
  if (!wp) return NULL;
  for (i = 0; mode[i] && i < 7; i++) wm[i] = (wchar_t)(unsigned char)mode[i];
  wm[i] = 0;
  f = _wfopen(wp, wm);
  free(wp);
  return f;
}

int plat_remove(const char *path) {
  wchar_t *w = win_wide(path);
  BOOL ok;
  if (!w) return -1;
  ok = DeleteFileW(w);
  free(w);
  return ok ? 0 : -1;
}

int plat_rename(const char *from, const char *to) {
  wchar_t *wfrom = win_wide(from);
  wchar_t *wto = win_wide(to);
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

/* %APPDATA%\mote\config. APPDATA is read as UTF-16: the ANSI value cannot
   hold a Cyrillic user name on a non-Cyrillic system locale. */
int plat_config_path(char *out, size_t n) {
  const wchar_t *home = _wgetenv(L"APPDATA");
  char *dir;
  wchar_t *wdir;
  int r = -1;
  if (!home || !home[0] || !(dir = win_utf8(home))) return -1;
  if (mote_snprintf(out, n, "%s\\%s", dir, MOTE_NAME) < (int)n && (wdir = win_wide(out))) {
    _wmkdir(wdir);
    free(wdir);
    if (mote_snprintf(out, n, "%s\\%s\\config", dir, MOTE_NAME) < (int)n) r = 0;
  }
  free(dir);
  return r;
}
