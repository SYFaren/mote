/* mote core — minimal regex matcher for /pattern/ search */
#ifndef MOTE_REGEX_H
#define MOTE_REGEX_H

#include "buffer.h"
#include "mote_ansi.h"

/* Match regex at buf[pos..]; returns match length or 0. */
size_t re_match_buf(const Buf *b, size_t pos, const char *pat, mote_bool caseless);

#endif
