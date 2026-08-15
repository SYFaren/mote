/* mote/mote-x/windows — developer: SYFaren */
#include "utf8.h"

static int utf8_cont(unsigned char c) { return (c & 0xC0) == 0x80; }

int utf8_decode(const char *s, size_t n, uint32_t *out) {
  unsigned char c0;
  uint32_t cp;
  if (!n) return 0;
  c0 = (unsigned char)s[0];
  if (c0 < 0x80) {
    *out = c0;
    return 1;
  }
  if ((c0 & 0xE0) == 0xC0) {
    if (n < 2 || c0 < 0xC2 || !utf8_cont((unsigned char)s[1])) {
      *out = 0xFFFD;
      return 1;
    }
    *out = ((c0 & 0x1Fu) << 6) | ((unsigned char)s[1] & 0x3Fu);
    return 2;
  }
  if ((c0 & 0xF0) == 0xE0) {
    if (n < 3 || !utf8_cont((unsigned char)s[1]) ||
        !utf8_cont((unsigned char)s[2])) {
      *out = 0xFFFD;
      return 1;
    }
    cp = ((c0 & 0x0Fu) << 12) | (((unsigned char)s[1] & 0x3Fu) << 6) |
         ((unsigned char)s[2] & 0x3Fu);
    if (cp < 0x800u || (cp >= 0xD800u && cp <= 0xDFFFu)) {
      *out = 0xFFFD;
      return 1;
    }
    *out = cp;
    return 3;
  }
  if ((c0 & 0xF8) == 0xF0) {
    if (n < 4 || c0 > 0xF4 || !utf8_cont((unsigned char)s[1]) ||
        !utf8_cont((unsigned char)s[2]) || !utf8_cont((unsigned char)s[3])) {
      *out = 0xFFFD;
      return 1;
    }
    cp = ((c0 & 0x07u) << 18) | (((unsigned char)s[1] & 0x3Fu) << 12) |
         (((unsigned char)s[2] & 0x3Fu) << 6) | ((unsigned char)s[3] & 0x3Fu);
    if (cp < 0x10000u || cp > 0x10FFFFu) {
      *out = 0xFFFD;
      return 1;
    }
    *out = cp;
    return 4;
  }
  *out = 0xFFFD;
  return 1;
}

int utf8_encode(uint32_t cp, char out[4]) {
  if (cp < 0x80) { out[0] = (char)cp; return 1; }
  if (cp < 0x800) {
    out[0] = (char)(0xC0 | (cp >> 6));
    out[1] = (char)(0x80 | (cp & 0x3F));
    return 2;
  }
  if (cp < 0x10000) {
    out[0] = (char)(0xE0 | (cp >> 12));
    out[1] = (char)(0x80 | ((cp >> 6) & 0x3F));
    out[2] = (char)(0x80 | (cp & 0x3F));
    return 3;
  }
  out[0] = (char)(0xF0 | (cp >> 18));
  out[1] = (char)(0x80 | ((cp >> 12) & 0x3F));
  out[2] = (char)(0x80 | ((cp >> 6) & 0x3F));
  out[3] = (char)(0x80 | (cp & 0x3F));
  return 4;
}

size_t utf8_prev(const char *s, size_t i) {
  if (!i) return 0;
  i--;
  while (i && ((unsigned char)s[i] & 0xC0) == 0x80) i--;
  return i;
}

size_t utf8_next(const char *s, size_t n, size_t i) {
  uint32_t cp;
  int len;
  if (i >= n) return n;
  len = utf8_decode(s + i, n - i, &cp);
  return i + (size_t)(len > 0 ? len : 1);
}
