/* mote core — hl.h */
#ifndef MOTE_HL_H
#define MOTE_HL_H

#include "mote_ansi.h"
#include <stddef.h>

typedef enum {
  HL_NORMAL = 0,
  HL_COMMENT,
  HL_STRING,
  HL_NUMBER,
  HL_KEYWORD,
  HL_TYPE,
  HL_PREPROC,
  HL_MATCH,
  HL_BRACKET
} HlKind;

typedef struct {
  mote_u16 start, len;
  unsigned char kind;
} HlSpan;

#define HL_MAX_SPANS 128

typedef struct HlSyntax HlSyntax;

const HlSyntax *hl_select(const char *path);
int hl_line(const HlSyntax *syn, const char *line, size_t len, int in_ml,
            HlSpan *out, int max_out, int *out_ml);
HlKind hl_kind_at(const HlSpan *spans, int nspans, size_t off);
const char *hl_lang_name(const HlSyntax *syn);
int hl_has_multiline(const HlSyntax *syn);

#endif
