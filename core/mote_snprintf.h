/* mote core — bounded sprintf (ANSI C89; formats are ours) */
#ifndef MOTE_SNPRINTF_H
#define MOTE_SNPRINTF_H

#include <stddef.h>
#include <stdarg.h>

int mote_snprintf(char *dst, size_t n, const char *fmt, ...);
int mote_vsnprintf(char *dst, size_t n, const char *fmt, va_list ap);

#endif
