/* mote/mote/linux — developer: SYFaren */
#ifndef MOTE_UNDO_H
#define MOTE_UNDO_H
#include <stddef.h>
#include <stdbool.h>

#define MOTE_UNDO_MAX 512

typedef enum { U_INSERT, U_DELETE } UndoKind;

typedef struct {
  UndoKind kind;
  size_t pos;
  char *text;
  size_t len;
} UndoAct;

typedef struct {
  UndoAct *items;
  size_t n, capa, head;
} UndoStack;

void undo_init(UndoStack *u);
void undo_free(UndoStack *u);
/* Push; coalesces adjacent single-rune inserts when coalesce=true. */
bool undo_push(UndoStack *u, UndoKind kind, size_t pos, const char *text,
               size_t len, bool coalesce);
UndoAct *undo_pop_undo(UndoStack *u);
UndoAct *undo_pop_redo(UndoStack *u);

#endif
