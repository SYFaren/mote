#ifndef MOTE_SNPRINTF_H
#define MOTE_SNPRINTF_H

#include <stddef.h>
#include <stdarg.h>

/* C89 has no vsnprintf. Handles %s %c %d %i %u %x %X %%, the l length,
   '-' and '0' flags, width and precision (both may be '*'). Returns the
   length the full output needs, like C99, so r >= n means it was cut. */
int mote_snprintf(char *dst, size_t n, const char *fmt, ...);
int mote_vsnprintf(char *dst, size_t n, const char *fmt, va_list ap);

#endif
