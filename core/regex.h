#ifndef MOTE_REGEX_H
#define MOTE_REGEX_H

#include "buffer.h"
#include "mote_ansi.h"

/* Length of the match at pos, or 0. Knows . * + ? [] [^] \d \w \s and
   escapes; ^ and $ match at line starts and ends. */
size_t re_match_buf(const Buf *b, size_t pos, const char *pat, mote_bool caseless);

#endif
