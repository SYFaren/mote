/* mote core — directory listing (POSIX / DJGPP / Win32) */
#include "dirlist.h"
#include "mote_snprintf.h"
#include <string.h>

#if defined(_WIN32) && !defined(__DJGPP__)
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

static int push_name(char out[DIRLIST_MAX][256], int n, int max, const char *name) {
  if (n >= max || !name || !name[0] || name[0] == '.') return n;
  if (strlen(name) >= 256) return n;
  strcpy(out[n], name);
  return n + 1;
}

int dirlist_files(const char *dir, char out[DIRLIST_MAX][256], int max_out) {
  wchar_t wdir[512];
  WIN32_FIND_DATAW fd;
  HANDLE h;
  wchar_t spec[520];
  int n = 0;
  if (!dir || !out || max_out <= 0) return 0;
  if (MultiByteToWideChar(CP_UTF8, 0, dir, -1, wdir, (int)(sizeof wdir / sizeof wdir[0])) <= 0)
    return 0;
  if (swprintf(spec, sizeof spec / sizeof spec[0], L"%ls\\*", wdir) <= 0) return 0;
  h = FindFirstFileW(spec, &fd);
  if (h == INVALID_HANDLE_VALUE) return 0;
  do {
    char name[256];
    if (fd.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) continue;
    if (WideCharToMultiByte(CP_UTF8, 0, fd.cFileName, -1, name, (int)sizeof name, NULL, NULL) <= 0)
      continue;
    n = push_name(out, n, max_out, name);
  } while (FindNextFileW(h, &fd));
  FindClose(h);
  return n;
}

#else
#include <dirent.h>
#include <sys/stat.h>

static int push_name(char out[DIRLIST_MAX][256], int n, int max, const char *name) {
  if (n >= max || !name || !name[0] || name[0] == '.') return n;
  if (strlen(name) >= 256) return n;
  strcpy(out[n], name);
  return n + 1;
}

int dirlist_files(const char *dir, char out[DIRLIST_MAX][256], int max_out) {
  DIR *d;
  struct dirent *e;
  struct stat st;
  char path[1024];
  int n = 0;
  if (!dir || !out || max_out <= 0) return 0;
  d = opendir(dir);
  if (!d) return 0;
  while ((e = readdir(d)) != NULL) {
    if (mote_snprintf(path, sizeof path, "%s/%s", dir, e->d_name) >= (int)sizeof path)
      continue;
    if (stat(path, &st) != 0 || !S_ISREG(st.st_mode)) continue;
    n = push_name(out, n, max_out, e->d_name);
  }
  closedir(d);
  return n;
}
#endif
