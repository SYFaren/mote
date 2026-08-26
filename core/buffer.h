/* mote core — buffer.h */
#ifndef MOTE_BUFFER_H
#define MOTE_BUFFER_H

#include "common.h"
#include <stddef.h>

typedef struct {
  char *data;
  size_t capa, gap_start, gap_end;
} Buf;

mote_bool buf_init(Buf *b, size_t hint);
void buf_free(Buf *b);
size_t buf_len(const Buf *b);
mote_bool buf_insert(Buf *b, size_t pos, const char *s, size_t n);
mote_bool buf_delete(Buf *b, size_t pos, size_t n);
void buf_get(const Buf *b, size_t pos, size_t n, char *dst);
char buf_at(const Buf *b, size_t pos);
mote_bool buf_match(const Buf *b, size_t pos, const char *s, size_t n);
mote_bool buf_match_ci(const Buf *b, size_t pos, const char *s, size_t n);
void buf_shrink_gap(Buf *b);
char *buf_strdup(const Buf *b);
mote_bool buf_load(Buf *b, const char *path);
mote_bool buf_save(const Buf *b, const char *path);
void buf_seek(Buf *b, size_t pos);

#endif
