/* mote/mote-x/windows — developer: SYFaren */

#include "buffer.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <io.h>
#include <fcntl.h>
#include <errno.h>

#define GAP_MIN 64

static size_t gap_size(const Buf *b) { return b->gap_end - b->gap_start; }

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

FILE *mote_fopen(const char *path, const char *mode) {
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

static void mote_unlink(const char *path) {
  wchar_t *w = utf8_to_wide(path);
  if (!w) return;
  DeleteFileW(w);
  free(w);
}

static bool mote_replace(const char *from_utf8, const char *to_utf8) {
  wchar_t *wfrom = utf8_to_wide(from_utf8);
  wchar_t *wto = utf8_to_wide(to_utf8);
  BOOL ok;
  if (!wfrom || !wto) {
    free(wfrom);
    free(wto);
    return false;
  }
  ok = MoveFileExW(wfrom, wto, MOVEFILE_REPLACE_EXISTING | MOVEFILE_COPY_ALLOWED);
  free(wfrom);
  free(wto);
  return ok != 0;
}

bool buf_init(Buf *b, size_t hint) {
  size_t c = hint < GAP_MIN ? GAP_MIN * 2 : hint + GAP_MIN;
  if (c > MOTE_MAX_FILE + GAP_MIN) c = MOTE_MAX_FILE + GAP_MIN;
  memset(b, 0, sizeof *b);
  b->data = (char *)malloc(c);
  if (!b->data) return false;
  b->capa = c;
  b->gap_start = 0;
  b->gap_end = c;
  return true;
}

void buf_free(Buf *b) {
  free(b->data);
  memset(b, 0, sizeof *b);
}

size_t buf_len(const Buf *b) {
  if (!b->data || b->gap_end < b->gap_start || b->capa < b->gap_end)
    return 0;
  return b->capa - gap_size(b);
}

static bool grow(Buf *b, size_t need) {
  size_t len = buf_len(b), nc, after;
  char *nd;
  if (gap_size(b) >= need) return true;
  if (len > MOTE_MAX_FILE || need > MOTE_MAX_FILE - len) return false;
  nc = b->capa ? b->capa * 2 : GAP_MIN * 2;
  if (nc < len + need + GAP_MIN) nc = len + need + GAP_MIN;
  if (nc < b->capa) return false; /* overflow */
  nd = (char *)realloc(b->data, nc);
  if (!nd) return false;
  after = b->capa - b->gap_end;
  if (after) memmove(nd + nc - after, nd + b->gap_end, after);
  b->data = nd;
  b->gap_end = nc - after;
  b->capa = nc;
  return true;
}

static void move_gap(Buf *b, size_t pos) {
  size_t len = buf_len(b);
  if (pos > len) pos = len;
  if (pos < b->gap_start) {
    size_t n = b->gap_start - pos;
    memmove(b->data + b->gap_end - n, b->data + pos, n);
    b->gap_start = pos;
    b->gap_end -= n;
  } else if (pos > b->gap_start) {
    size_t n = pos - b->gap_start;
    memmove(b->data + b->gap_start, b->data + b->gap_end, n);
    b->gap_start += n;
    b->gap_end += n;
  }
}

bool buf_insert(Buf *b, size_t pos, const char *s, size_t n) {
  if (!n) return true;
  if (!b->data || !s) return false;
  if (!grow(b, n)) return false;
  move_gap(b, pos);
  memcpy(b->data + b->gap_start, s, n);
  b->gap_start += n;
  return true;
}

bool buf_delete(Buf *b, size_t pos, size_t n) {
  size_t len = buf_len(b);
  if (!b->data || !n || pos >= len) return true;
  if (pos + n > len) n = len - pos;
  move_gap(b, pos);
  b->gap_end += n;
  buf_shrink_gap(b);
  return true;
}

char buf_at(const Buf *b, size_t pos) {
  size_t len = buf_len(b);
  if (!b->data || pos >= len) return 0;
  if (pos < b->gap_start) return b->data[pos];
  return b->data[pos + gap_size(b)];
}

bool buf_match(const Buf *b, size_t pos, const char *s, size_t n) {
  size_t i, len;
  if (!s || !n) return true;
  len = buf_len(b);
  if (pos + n > len) return false;
  for (i = 0; i < n; i++) {
    if (buf_at(b, pos + i) != s[i]) return false;
  }
  return true;
}

bool buf_match_ci(const Buf *b, size_t pos, const char *s, size_t n) {
  size_t i, len;
  if (!s || !n) return true;
  len = buf_len(b);
  if (pos + n > len) return false;
  for (i = 0; i < n; i++) {
    unsigned char a = (unsigned char)buf_at(b, pos + i);
    unsigned char c = (unsigned char)s[i];
    if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 32);
    if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
    if (a != c) return false;
  }
  return true;
}

/* Reclaim oversized gap after heavy deletes (stability / RAM). */
void buf_shrink_gap(Buf *b) {
  size_t len, g, nc, after;
  char *nd;
  if (!b->data) return;
  len = buf_len(b);
  g = gap_size(b);
  if (g <= GAP_MIN * 4) return;
  if (g <= len) return;
  nc = len + GAP_MIN * 2;
  if (nc >= b->capa) return;
  after = b->capa - b->gap_end;
  nd = (char *)malloc(nc);
  if (!nd) return;
  if (b->gap_start) memcpy(nd, b->data, b->gap_start);
  if (after) memcpy(nd + nc - after, b->data + b->gap_end, after);
  free(b->data);
  b->data = nd;
  b->capa = nc;
  b->gap_end = nc - after;
}

void buf_get(const Buf *b, size_t pos, size_t n, char *dst) {
  size_t len, left;
  if (!b->data || !dst || !n) return;
  len = buf_len(b);
  if (pos >= len) return;
  if (pos + n > len) n = len - pos;
  if (pos + n <= b->gap_start) {
    memcpy(dst, b->data + pos, n);
  } else if (pos >= b->gap_start) {
    memcpy(dst, b->data + pos + gap_size(b), n);
  } else {
    left = b->gap_start - pos;
    memcpy(dst, b->data + pos, left);
    memcpy(dst + left, b->data + b->gap_end, n - left);
  }
}

char *buf_strdup(const Buf *b) {
  size_t len = buf_len(b);
  char *s = (char *)malloc(len + 1);
  if (!s) return NULL;
  if (len) buf_get(b, 0, len, s);
  s[len] = 0;
  return s;
}

bool buf_load(Buf *b, const char *path) {
  FILE *f;
  long sz;
  size_t n;
  if (!path || !path[0]) return false;
  buf_free(b);
  f = mote_fopen(path, "rb");
  if (!f) return false;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return false;
  }
  sz = ftell(f);
  if (sz < 0 || (size_t)sz > MOTE_MAX_FILE) {
    fclose(f);
    return false;
  }
  rewind(f);
  if (!buf_init(b, (size_t)sz)) {
    fclose(f);
    return false;
  }
  n = sz ? fread(b->data, 1, (size_t)sz, f) : 0;
  fclose(f);
  if (n != (size_t)sz) {
    buf_free(b);
    return false;
  }
  b->gap_start = (size_t)sz;
  b->gap_end = b->capa;
  return true;
}

bool buf_save(const Buf *b, const char *path) {
  char tmp[4096];
  FILE *f;
  size_t len, i, n;
  int fd;
  if (!path || !path[0] || !b->data) return false;
  len = buf_len(b);
  if (snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp) return false;
  f = mote_fopen(tmp, "wb");
  if (!f) return false;
  for (i = 0; i < len; i += n) {
    char chunk[4096];
    n = len - i;
    if (n > sizeof chunk) n = sizeof chunk;
    buf_get(b, i, n, chunk);
    if (fwrite(chunk, 1, n, f) != n) {
      fclose(f);
      mote_unlink(tmp);
      return false;
    }
  }
  if (fflush(f) != 0) {
    fclose(f);
    mote_unlink(tmp);
    return false;
  }
  fd = fileno(f);
  if (fd >= 0) (void)_commit(fd);
  if (fclose(f) != 0) {
    mote_unlink(tmp);
    return false;
  }
  if (!mote_replace(tmp, path)) {
    mote_unlink(tmp);
    return false;
  }
  return true;
}

void buf_seek(Buf *b, size_t pos) {
  if (!b->data) return;
  move_gap(b, pos);
}
