#include "recovery.h"
#include "mote_snprintf.h"
#include <stdlib.h>
#include <string.h>
#include <errno.h>

/* Explicit decimal fields: portable across word sizes and endianness. */
static mote_bool number(FILE *f, unsigned long *v) {
  char line[32], *end;
  unsigned long n;
  if (!fgets(line, sizeof line, f) || line[0] < '0' || line[0] > '9')
    return MOTE_FALSE;
  errno = 0;
  n = strtoul(line, &end, 10);
  if (errno || *end != '\n' || end[1]) return MOTE_FALSE;
  *v = n;
  return MOTE_TRUE;
}

mote_bool recovery_begin(Recovery *r) {
  char cfg[640], lockpath[1100];
  Recovery spare;
  int i;
  memset(r, 0, sizeof *r);
  memset(&spare, 0, sizeof spare);
  if (plat_config_path(cfg, sizeof cfg) != 0) return MOTE_FALSE;
  for (i = 0; i < 16; i++) {
    mote_snprintf(r->path, sizeof r->path, "%s.recovery-%d", cfg, i);
    mote_snprintf(lockpath, sizeof lockpath, "%s.lock", r->path);
    r->lock = plat_lock_file(lockpath);
    if (!r->lock) continue;
    if (recovery_exists(r)) {
      recovery_end(&spare, MOTE_FALSE);
      return MOTE_TRUE;
    }
    if (!spare.lock) spare = *r;
    else recovery_end(r, MOTE_FALSE);
  }
  *r = spare;
  return r->lock ? MOTE_TRUE : MOTE_FALSE;
}

mote_bool recovery_exists(const Recovery *r) {
  FILE *f;
  if (!r->lock) return MOTE_FALSE;
  f = plat_fopen(r->path, "rb");
  if (!f) return MOTE_FALSE;
  fclose(f);
  return MOTE_TRUE;
}

mote_bool recovery_checkpoint(Recovery *r, const Editor *e) {
  char tmp[1100], block[4096];
  FILE *f;
  int i, count = 0, ok = 1;
  size_t pos, len, n;
  if (!r->lock) return MOTE_FALSE;
  for (i = 0; i < e->ndocs; i++) if (e->docs[i].dirty) count++;
  if (!count) {
    if (recovery_exists(r) && plat_remove(r->path) != 0) return MOTE_FALSE;
    r->revision = e->recovery_revision;
    return MOTE_TRUE;
  }
  mote_snprintf(tmp, sizeof tmp, "%s.tmp", r->path);
  f = plat_private_write(tmp);
  if (!f) return MOTE_FALSE;
  if (fprintf(f, "MOTE-RECOVERY-1\n%d\n", count) < 0) ok = 0;
  for (i = 0; i < e->ndocs && ok; i++) {
    const Doc *d = &e->docs[i];
    if (!d->dirty) continue;
    len = buf_len(&d->buf);
    n = strlen(d->path);
    if (fprintf(f, "%lu\n%lu\n%d\n", (unsigned long)n,
                (unsigned long)len, (int)d->eol) < 0 ||
        fwrite(d->path, 1, n, f) != n) ok = 0;
    for (pos = 0; pos < len && ok; pos += n) {
      n = len - pos;
      if (n > sizeof block) n = sizeof block;
      buf_get(&d->buf, pos, n, block);
      if (fwrite(block, 1, n, f) != n) ok = 0;
    }
  }
  if (fflush(f) != 0) ok = 0;
  plat_fsync_file(f);
  if (ferror(f)) ok = 0;
  if (fclose(f) != 0) ok = 0;
  if (ok && plat_rename(tmp, r->path) == 0) {
    r->revision = e->recovery_revision;
    return MOTE_TRUE;
  }
  plat_remove(tmp);
  return MOTE_FALSE;
}

mote_bool recovery_restore(Recovery *r, Editor *e) {
  Editor *restored;
  FILE *f;
  char magic[32];
  unsigned long count, pn, len, eol;
  int i, ok = 1;
  if (!r->lock) return MOTE_FALSE;
  f = plat_fopen(r->path, "rb");
  if (!f) return MOTE_FALSE;
  if (!fgets(magic, sizeof magic, f) || strcmp(magic, "MOTE-RECOVERY-1\n") ||
      !number(f, &count) || count < 1 || count > MAX_DOCS) {
    fclose(f);
    return MOTE_FALSE;
  }
  restored = (Editor *)calloc(1, sizeof *restored);
  if (!restored) { fclose(f); return MOTE_FALSE; }
  if (!ed_init(restored)) { free(restored); fclose(f); return MOTE_FALSE; }
  for (i = 0; i < (int)count && ok; i++) {
    Doc *d;
    if (i) ed_new_doc(restored);
    d = &restored->docs[i];
    if (restored->ndocs != i + 1 || !number(f, &pn) ||
        pn >= sizeof d->path || !number(f, &len) || len > MOTE_MAX_FILE ||
        !number(f, &eol) || eol > EOL_CRLF) { ok = 0; break; }
    if (fread(d->path, 1, (size_t)pn, f) != (size_t)pn ||
        memchr(d->path, 0, (size_t)pn)) { ok = 0; break; }
    d->path[pn] = 0;
    /* A later launch can have a different working directory. Do not let a
       relative recovered name silently save over an unrelated file. */
    if (d->path[0] != '/' &&
        !(d->path[0] && d->path[1] == ':' &&
          (d->path[2] == '/' || d->path[2] == '\\')) &&
        !(d->path[0] == '\\' && d->path[1] == '\\')) d->path[0] = 0;
    buf_free(&d->buf);
    if (!buf_init(&d->buf, (size_t)len)) { ok = 0; break; }
    if (fread(d->buf.data, 1, (size_t)len, f) != (size_t)len) { ok = 0; break; }
    d->buf.gap_start = (size_t)len;
    d->buf.gap_end = d->buf.capa;
    d->eol = (EolMode)eol;
    d->dirty = MOTE_TRUE;
  }
  if (ok && (fgetc(f) != EOF || ferror(f))) ok = 0;
  fclose(f);
  if (ok) {
    /* Transfer only documents; preserve loaded preferences and recent paths. */
    for (i = 0; i < e->ndocs; i++) {
      buf_free(&e->docs[i].buf);
      undo_free(&e->docs[i].undo);
      free(e->docs[i].lines.off);
    }
    free(e->vrow_cache);
    e->vrow_cache = NULL;
    e->vrow_n = 0;
    memcpy(e->docs, restored->docs, sizeof e->docs);
    e->ndocs = restored->ndocs;
    e->cur = 0;
    e->mode = MODE_EDIT;
    e->need_draw = MOTE_TRUE;
    e->recovery_revision++;
    restored->ndocs = 0;
  }
  ed_free(restored);
  free(restored);
  return ok ? MOTE_TRUE : MOTE_FALSE;
}

void recovery_end(Recovery *r, mote_bool discard) {
  if (!r->lock) return;
  if (discard) plat_remove(r->path);
  /* A crash can leave a private partial temporary snapshot behind. It is
     never restored, and only the owning slot may remove it. */
  if (discard) {
    char tmp[1100];
    mote_snprintf(tmp, sizeof tmp, "%s.tmp", r->path);
    plat_remove(tmp);
  }
  fclose(r->lock);
  r->lock = NULL;
}
