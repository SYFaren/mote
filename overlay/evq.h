/* mote overlay — event queue and typed-text batching shared by overlays */
#ifndef MOTE_EVQ_H
#define MOTE_EVQ_H

#include "platform.h"
#include <string.h>

#define EVQ_CAP 256

typedef struct {
  PlatEvent ev[EVQ_CAP];
  int n;
  char text[sizeof ((PlatEvent *)0)->text]; /* typed bytes, sent as one PE_TEXT */
  int text_n;
} EvQueue;

static inline void evq_push(EvQueue *q, const PlatEvent *e) {
  if (q->n < EVQ_CAP) q->ev[q->n++] = *e;
}

static inline mote_bool evq_pop(EvQueue *q, PlatEvent *e) {
  if (q->n <= 0) return MOTE_FALSE;
  *e = q->ev[0];
  q->n--;
  memmove(q->ev, q->ev + 1, (size_t)q->n * sizeof q->ev[0]);
  return MOTE_TRUE;
}

static inline void evq_type(EvQueue *q, PlatEventType type) {
  PlatEvent e;
  memset(&e, 0, sizeof e);
  e.type = type;
  evq_push(q, &e);
}

static inline void evq_flush_text(EvQueue *q) {
  PlatEvent e;
  if (q->text_n <= 0) return;
  memset(&e, 0, sizeof e);
  e.type = PE_TEXT;
  memcpy(e.text, q->text, (size_t)q->text_n);
  e.text_len = q->text_n;
  q->text_n = 0;
  evq_push(q, &e);
}

static inline void evq_text(EvQueue *q, const char *s, int n) {
  int i;
  for (i = 0; i < n; i++) {
    if (q->text_n >= (int)sizeof q->text) evq_flush_text(q);
    q->text[q->text_n++] = s[i];
  }
}

/* A key ends any pending text first, so events keep their typed order. */
static inline void evq_key(EvQueue *q, PlatKey k, mote_bool ctrl, mote_bool shift) {
  PlatEvent e;
  evq_flush_text(q);
  memset(&e, 0, sizeof e);
  e.type = PE_KEY;
  e.key = k;
  e.ctrl = ctrl;
  e.shift = shift;
  evq_push(q, &e);
}

#endif
