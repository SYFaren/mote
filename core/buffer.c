/* mote core — buffer.c */
#include "buffer.h"
#include "platform.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "mote_snprintf.h"

#define GAP_MIN 64

static size_t gap_size(const Buf *b) { return b->gap_end - b->gap_start; }

mote_bool buf_init(Buf *b, size_t hint) {
  size_t c = hint < GAP_MIN ? GAP_MIN * 2 : hint + GAP_MIN;
  if (c > MOTE_MAX_FILE + GAP_MIN) c = MOTE_MAX_FILE + GAP_MIN;
  memset(b, 0, sizeof *b);
  b->data = (char *)malloc(c);
  if (!b->data) return MOTE_FALSE;
  b->capa = c;
  b->gap_start = 0;
  b->gap_end = c;
  return MOTE_TRUE;
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

static mote_bool grow(Buf *b, size_t need) {
  size_t len = buf_len(b), nc, after;
  char *nd;
  if (gap_size(b) >= need) return MOTE_TRUE;
  if (len > MOTE_MAX_FILE || need > MOTE_MAX_FILE - len) return MOTE_FALSE;
  nc = b->capa ? b->capa * 2 : GAP_MIN * 2;
  if (nc < len + need + GAP_MIN) nc = len + need + GAP_MIN;
  if (nc < b->capa) return MOTE_FALSE; /* overflow */
  nd = (char *)realloc(b->data, nc);
  if (!nd) return MOTE_FALSE;
  after = b->capa - b->gap_end;
  if (after) memmove(nd + nc - after, nd + b->gap_end, after);
  b->data = nd;
  b->gap_end = nc - after;
  b->capa = nc;
  return MOTE_TRUE;
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

mote_bool buf_insert(Buf *b, size_t pos, const char *s, size_t n) {
  if (!n) return MOTE_TRUE;
  if (!b->data || !s) return MOTE_FALSE;
  if (!grow(b, n)) return MOTE_FALSE;
  move_gap(b, pos);
  memcpy(b->data + b->gap_start, s, n);
  b->gap_start += n;
  return MOTE_TRUE;
}

mote_bool buf_delete(Buf *b, size_t pos, size_t n) {
  size_t len = buf_len(b);
  if (!b->data || !n || pos >= len) return MOTE_TRUE;
  if (pos + n > len) n = len - pos;
  move_gap(b, pos);
  b->gap_end += n;
  buf_shrink_gap(b);
  return MOTE_TRUE;
}

char buf_at(const Buf *b, size_t pos) {
  size_t len = buf_len(b);
  if (!b->data || pos >= len) return 0;
  if (pos < b->gap_start) return b->data[pos];
  return b->data[pos + gap_size(b)];
}

static mote_bool buf_match_ex(const Buf *b, size_t pos, const char *s, size_t n,
                               int ci) {
  size_t i, len;
  if (!s || !n) return MOTE_TRUE;
  len = buf_len(b);
  if (pos + n > len) return MOTE_FALSE;
  for (i = 0; i < n; i++) {
    unsigned char a = (unsigned char)buf_at(b, pos + i);
    unsigned char c = (unsigned char)s[i];
    if (ci) {
      if (a >= 'A' && a <= 'Z') a = (unsigned char)(a + 32);
      if (c >= 'A' && c <= 'Z') c = (unsigned char)(c + 32);
    }
    if (a != c) return MOTE_FALSE;
  }
  return MOTE_TRUE;
}

mote_bool buf_match(const Buf *b, size_t pos, const char *s, size_t n) {
  return buf_match_ex(b, pos, s, n, 0);
}

mote_bool buf_match_ci(const Buf *b, size_t pos, const char *s, size_t n) {
  return buf_match_ex(b, pos, s, n, 1);
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

mote_bool buf_load(Buf *b, const char *path) {
  FILE *f;
  long sz;
  size_t n;
  if (!path || !path[0]) return MOTE_FALSE;
  buf_free(b);
  f = plat_fopen(path, "rb");
  if (!f) return MOTE_FALSE;
  if (fseek(f, 0, SEEK_END) != 0) {
    fclose(f);
    return MOTE_FALSE;
  }
  sz = ftell(f);
  if (sz < 0 || (size_t)sz > MOTE_MAX_FILE) {
    fclose(f);
    return MOTE_FALSE;
  }
  rewind(f);
  if (!buf_init(b, (size_t)sz)) {
    fclose(f);
    return MOTE_FALSE;
  }
  n = sz ? fread(b->data, 1, (size_t)sz, f) : 0;
  fclose(f);
  if (n != (size_t)sz) {
    buf_free(b);
    return MOTE_FALSE;
  }
  b->gap_start = (size_t)sz;
  b->gap_end = b->capa;
  return MOTE_TRUE;
}

mote_bool buf_save(const Buf *b, const char *path) {
  char tmp[4096];
  FILE *f;
  size_t len, i, n;
  if (!path || !path[0] || !b->data) return MOTE_FALSE;
  len = buf_len(b);
  /* Temp name must be legal on DOS 8.3 (A.C.tmp is not). Keep Unix-style
   * path.tmp elsewhere for unique parallel saves. */
#if defined(__DJGPP__) || defined(__MSDOS__) || defined(MSDOS)
  {
    const char *slash = strrchr(path, '/');
    const char *bslash = strrchr(path, '\\');
    const char *dirend = path;
    size_t dlen;
    if (bslash && (!slash || bslash > slash)) slash = bslash;
    if (slash) dirend = slash + 1;
    dlen = (size_t)(dirend - path);
    if (dlen + 12 >= sizeof tmp) return MOTE_FALSE;
    if (dlen) memcpy(tmp, path, dlen);
    mote_snprintf(tmp + dlen, sizeof tmp - dlen, "MT$$$$$.TMP");
  }
#else
  if (mote_snprintf(tmp, sizeof tmp, "%s.tmp", path) >= (int)sizeof tmp)
    return MOTE_FALSE;
#endif
  f = plat_fopen(tmp, "wb");
  if (!f) return MOTE_FALSE;
  for (i = 0; i < len; i += n) {
    char chunk[4096];
    n = len - i;
    if (n > sizeof chunk) n = sizeof chunk;
    buf_get(b, i, n, chunk);
    if (fwrite(chunk, 1, n, f) != n) {
      fclose(f);
      plat_remove(tmp);
      return MOTE_FALSE;
    }
  }
  if (fflush(f) != 0) {
    fclose(f);
    plat_remove(tmp);
    return MOTE_FALSE;
  }
  plat_fsync_file(f);
  if (fclose(f) != 0) {
    plat_remove(tmp);
    return MOTE_FALSE;
  }
#if defined(__DJGPP__) || defined(__MSDOS__) || defined(MSDOS)
  /* FAT rename does not replace an existing name. */
  (void)plat_remove(path);
#endif
  if (plat_rename(tmp, path) != 0) {
    plat_remove(tmp);
    return MOTE_FALSE;
  }
  return MOTE_TRUE;
}

void buf_seek(Buf *b, size_t pos) {
  if (!b->data) return;
  move_gap(b, pos);
}
