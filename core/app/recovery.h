#ifndef MOTE_RECOVERY_H
#define MOTE_RECOVERY_H

#include "editor.h"

typedef struct {
  FILE *lock;
  char path[1024];
  unsigned long revision;
} Recovery;

/* One locked slot per instance, up to 16 concurrent instances. */
mote_bool recovery_begin(Recovery *r);
mote_bool recovery_exists(const Recovery *r);
mote_bool recovery_restore(Recovery *r, Editor *e);
mote_bool recovery_checkpoint(Recovery *r, const Editor *e);
void recovery_end(Recovery *r, mote_bool discard);

#endif
