/* mote/mote/linux — developer: SYFaren */
#ifndef MOTE_UTF8_H
#define MOTE_UTF8_H
#include <stddef.h>
#include <stdint.h>
int utf8_decode(const char *s, size_t n, uint32_t *out);
int utf8_encode(uint32_t cp, char out[4]);
size_t utf8_prev(const char *s, size_t i);
size_t utf8_next(const char *s, size_t n, size_t i);
#endif
