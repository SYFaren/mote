/* mote/mote-x/linux — developer: SYFaren */
#include "undo.h"
#include <stdlib.h>
#include <string.h>

void undo_init(UndoStack *u) { memset(u, 0, sizeof *u); }

static void free_act(UndoAct *a) {
  free(a->text);
  a->text = NULL;
  a->len = 0;
}

void undo_free(UndoStack *u) {
  size_t i;
  for (i = 0; i < u->n; i++) free_act(&u->items[i]);
  free(u->items);
  undo_init(u);
}

static void drop_oldest(UndoStack *u) {
  size_t i;
  if (!u->n) return;
  free_act(&u->items[0]);
  for (i = 1; i < u->n; i++) u->items[i - 1] = u->items[i];
  u->n--;
  if (u->head) u->head--;
}

bool undo_push(UndoStack *u, UndoKind kind, size_t pos, const char *text,
               size_t len, bool coalesce) {
  UndoAct *a;
  size_t i;
  char *nt;

  if (!text && len) return false;

  for (i = u->head; i < u->n; i++) free_act(&u->items[i]);
  u->n = u->head;

  /* Coalesce typing: extend last insert if contiguous and small. */
  if (coalesce && kind == U_INSERT && len > 0 && len <= 4 && u->head > 0) {
    a = &u->items[u->head - 1];
    if (a->kind == U_INSERT && a->pos + a->len == pos && a->len < 64) {
      nt = (char *)realloc(a->text, a->len + len);
      if (!nt) return false;
      memcpy(nt + a->len, text, len);
      a->text = nt;
      a->len += len;
      return true;
    }
  }

  while (u->n >= MOTE_UNDO_MAX) drop_oldest(u);

  if (u->n == u->capa) {
    size_t nc = u->capa ? u->capa * 2 : 32;
    UndoAct *ni = (UndoAct *)realloc(u->items, nc * sizeof(UndoAct));
    if (!ni) return false;
    u->items = ni;
    u->capa = nc;
  }
  a = &u->items[u->n];
  a->kind = kind;
  a->pos = pos;
  a->len = len;
  a->text = (char *)malloc(len ? len : 1);
  if (!a->text) return false;
  if (len) memcpy(a->text, text, len);
  u->n++;
  u->head = u->n;
  return true;
}

UndoAct *undo_pop_undo(UndoStack *u) {
  if (!u->head) return NULL;
  u->head--;
  return &u->items[u->head];
}

UndoAct *undo_pop_redo(UndoStack *u) {
  UndoAct *a;
  if (u->head >= u->n) return NULL;
  a = &u->items[u->head];
  u->head++;
  return a;
}
