/* mote core — mote_snprintf.c: bounded formatter for C89, which has no
   vsnprintf. Supports %s %c %d %i %u %x %X %%, the l length, '-' and '0'
   flags, width and precision (both may be '*'). Returns the full length
   the output needed, like C99 snprintf, so `r >= n` means truncated. */
#include "mote_snprintf.h"
#include <string.h>

typedef struct {
  char *dst;
  size_t n, len; /* capacity, characters produced so far */
} Out;

static void put(Out *o, char c) {
  if (o->len + 1 < o->n) o->dst[o->len] = c;
  o->len++;
}

static void put_padded(Out *o, const char *s, size_t sn, int width, int left, char pad) {
  size_t i, fill = width > 0 && (size_t)width > sn ? (size_t)width - sn : 0;
  if (!left)
    for (i = 0; i < fill; i++) put(o, pad);
  for (i = 0; i < sn; i++) put(o, s[i]);
  if (left)
    for (i = 0; i < fill; i++) put(o, ' ');
}

/* Digits of v in `base` into the end of buf; returns the first digit. */
static char *utoa(unsigned long v, unsigned base, int caps, char *end) {
  const char *digits = caps ? "0123456789ABCDEF" : "0123456789abcdef";
  *--end = 0;
  do {
    *--end = digits[v % base];
    v /= base;
  } while (v);
  return end;
}

int mote_vsnprintf(char *dst, size_t n, const char *fmt, va_list ap) {
  Out o;
  char num[24], *s;
  o.dst = dst;
  o.n = dst ? n : 0;
  o.len = 0;
  for (; fmt && *fmt; fmt++) {
    int left = 0, width = 0, prec = -1, lng = 0;
    char pad = ' ', conv;
    if (*fmt != '%') {
      put(&o, *fmt);
      continue;
    }
    for (fmt++; *fmt == '-' || *fmt == '0'; fmt++) {
      if (*fmt == '-') left = 1;
      else pad = '0';
    }
    if (*fmt == '*') {
      width = va_arg(ap, int);
      fmt++;
    }
    for (; *fmt >= '0' && *fmt <= '9'; fmt++) width = width * 10 + (*fmt - '0');
    if (*fmt == '.') {
      prec = 0;
      if (*++fmt == '*') {
        prec = va_arg(ap, int);
        fmt++;
      }
      for (; *fmt >= '0' && *fmt <= '9'; fmt++) prec = prec * 10 + (*fmt - '0');
    }
    if (*fmt == 'l') {
      lng = 1;
      fmt++;
    }
    conv = *fmt;
    if (left) pad = ' ';
    switch (conv) {
    case 's': {
      const char *str = va_arg(ap, const char *);
      size_t sn;
      if (!str) str = "(null)";
      sn = strlen(str);
      if (prec >= 0 && (size_t)prec < sn) sn = (size_t)prec;
      put_padded(&o, str, sn, width, left, ' ');
      break;
    }
    case 'c':
      num[0] = (char)va_arg(ap, int);
      put_padded(&o, num, 1, width, left, ' ');
      break;
    case 'd':
    case 'i': {
      long v = lng ? va_arg(ap, long) : va_arg(ap, int);
      unsigned long u = v < 0 ? 0ul - (unsigned long)v : (unsigned long)v;
      s = utoa(u, 10, 0, num + sizeof num);
      if (v < 0 && pad == '0') { /* sign goes before the zeros */
        put(&o, '-');
        if (width > 0) width--;
      } else if (v < 0) {
        *--s = '-';
      }
      put_padded(&o, s, strlen(s), width, left, pad);
      break;
    }
    case 'u':
    case 'x':
    case 'X': {
      unsigned long u = lng ? va_arg(ap, unsigned long) : va_arg(ap, unsigned);
      s = utoa(u, conv == 'u' ? 10u : 16u, conv == 'X', num + sizeof num);
      put_padded(&o, s, strlen(s), width, left, pad);
      break;
    }
    case '%':
      put(&o, '%');
      break;
    default: /* unsupported: emit it verbatim */
      put(&o, '%');
      if (conv) put(&o, conv);
      else fmt--; /* format ended after '%' */
      break;
    }
  }
  if (o.n) o.dst[o.len < o.n ? o.len : o.n - 1] = 0;
  return (int)o.len;
}

int mote_snprintf(char *dst, size_t n, const char *fmt, ...) {
  va_list ap;
  int r;
  va_start(ap, fmt);
  r = mote_vsnprintf(dst, n, fmt, ap);
  va_end(ap);
  return r;
}
