/* mote core — list files in a directory for quick-open */
#ifndef MOTE_DIRLIST_H
#define MOTE_DIRLIST_H

#define DIRLIST_MAX 256

/* Fill out[0..max_out-1] with basenames; returns count (sorted). */
int dirlist_files(const char *dir, char out[DIRLIST_MAX][256], int max_out);

#endif
