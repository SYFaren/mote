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
  if (!f) return;
  fflush(f);
  _commit(_fileno(f));
}

int plat_config_path(char *out, size_t n) {
  wchar_t *appdata = NULL;
  char *utf8 = NULL;
  char dir[1024];
  int len;
  (void)n;
  len = GetEnvironmentVariableW(L"APPDATA", NULL, 0);
  if (len <= 1) return -1;
  appdata = (wchar_t *)malloc((size_t)len * sizeof(wchar_t));
  if (!appdata) return -1;
  if (!GetEnvironmentVariableW(L"APPDATA", appdata, (DWORD)len)) {
    free(appdata);
    return -1;
  }
  utf8 = win_utf8(appdata);
  free(appdata);
  if (!utf8) return -1;
  if (mote_snprintf(dir, sizeof dir, "%s\\%s", utf8, MOTE_NAME) >= (int)sizeof dir) {
    free(utf8);
    return -1;
  }
  free(utf8);
  {
    wchar_t *wdir = win_wide(dir);
    if (wdir) {
      CreateDirectoryW(wdir, NULL);
      free(wdir);
    }
  }
  if (mote_snprintf(out, n, "%s\\config", dir) >= (int)n) return -1;
  return 0;
}
