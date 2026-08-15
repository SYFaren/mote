/* mote/mote/windows — developer: SYFaren */
#ifndef MOTE_BUFFER_H
#define MOTE_BUFFER_H
#include "common.h"
#include <stddef.h>
#include <stdbool.h>
#include <stdio.h>

typedef struct {
  char *data;
  size_t capa, gap_start, gap_end;
} Buf;

bool buf_init(Buf *b, size_t hint);
void buf_free(Buf *b);
size_t buf_len(const Buf *b);
bool buf_insert(Buf *b, size_t pos, const char *s, size_t n);
bool buf_delete(Buf *b, size_t pos, size_t n);
void buf_get(const Buf *b, size_t pos, size_t n, char *dst);
char buf_at(const Buf *b, size_t pos);
/* Compare [pos, pos+n) to s without allocating. */
bool buf_match(const Buf *b, size_t pos, const char *s, size_t n);
void buf_shrink_gap(Buf *b);
char *buf_strdup(const Buf *b);
bool buf_load(Buf *b, const char *path);
bool buf_save(const Buf *b, const char *path);
/* UTF-8 path open (Win32 uses wide CRT). */
FILE *mote_fopen(const char *path, const char *mode);
/* Move gap to pos — keep edits local (cache-friendly). */
void buf_seek(Buf *b, size_t pos);

#endif
