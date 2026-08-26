/* mote core — utf8.h */
#ifndef MOTE_UTF8_H
#define MOTE_UTF8_H

#include "mote_ansi.h"
#include <stddef.h>

int utf8_decode(const char *s, size_t n, mote_u32 *out);
int utf8_encode(mote_u32 cp, char out[4]);
size_t utf8_prev(const char *s, size_t i);

#endif
