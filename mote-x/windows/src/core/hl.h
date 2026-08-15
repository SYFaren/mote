/* mote/mote-x — developer: SYFaren — compact multi-lang highlighter */
#ifndef MOTE_HL_H
#define MOTE_HL_H
#include <stddef.h>
#include <stdint.h>

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
  uint16_t start, len;
  uint8_t kind;
} HlSpan;

#define HL_MAX_SPANS 128

typedef struct HlSyntax HlSyntax;

const HlSyntax *hl_select(const char *path);
/* Highlight one line; *in_ml is block-comment state in/out. Returns span count. */
int hl_line(const HlSyntax *syn, const char *line, size_t len, int in_ml,
            HlSpan *out, int max_out, int *out_ml);
HlKind hl_kind_at(const HlSpan *spans, int nspans, size_t off);
const char *hl_lang_name(const HlSyntax *syn);

#endif
