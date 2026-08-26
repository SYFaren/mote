/* mote core — mote_snprintf.c */
#include "mote_snprintf.h"
#include <stdio.h>
#include <string.h>

int mote_vsnprintf(char *dst, size_t n, const char *fmt, va_list ap) {
  char big[8192];
  int r;
  size_t copy;

  if (!dst || n == 0) return 0;
  dst[0] = '\0';
  if (!fmt) return 0;

  r = vsprintf(big, fmt, ap);
  if (r < 0) {
    dst[0] = '\0';
    return -1;
  }
  copy = (size_t)r;
  if (copy >= n) {
    memcpy(dst, big, n - 1);
    dst[n - 1] = '\0';
    return (int)(n - 1);
  }
  memcpy(dst, big, copy + 1);
  return r;
}

int mote_snprintf(char *dst, size_t n, const char *fmt, ...) {
  va_list ap;
  int r;
  va_start(ap, fmt);
  r = mote_vsnprintf(dst, n, fmt, ap);
  va_end(ap);
  return r;
}
