/* mote/mote-x/linux — buffer self-test — developer: SYFaren */
#define _POSIX_C_SOURCE 200809L
#include "buffer.h"
#include "utf8.h"
#include "undo.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int fail;

#define CHECK(cond)                                                            \
  do {                                                                         \
    if (!(cond)) {                                                             \
      fprintf(stderr, "FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);          \
      fail++;                                                                  \
    }                                                                          \
  } while (0)

int main(void) {
  Buf b;
  UndoStack u;
  char tmp[] = "/tmp/mote-x-test-XXXXXX";
  int fd;
  char *s;
  uint32_t cp;
  char enc[4];

  CHECK(buf_init(&b, 0));
  CHECK(buf_len(&b) == 0);
  CHECK(buf_insert(&b, 0, "hello", 5));
  CHECK(buf_len(&b) == 5);
  CHECK(buf_at(&b, 0) == 'h');
  CHECK(buf_at(&b, 4) == 'o');
  CHECK(buf_insert(&b, 5, "!", 1));
  CHECK(buf_delete(&b, 0, 1));
  CHECK(buf_at(&b, 0) == 'e');
  s = buf_strdup(&b);
  CHECK(s && strcmp(s, "ello!") == 0);
  free(s);

  /* gap move + get spanning gap */
  CHECK(buf_insert(&b, 2, "XX", 2));
  s = buf_strdup(&b);
  CHECK(s && strcmp(s, "elXXlo!") == 0);
  free(s);

  CHECK(buf_match(&b, 0, "el", 2));
  CHECK(!buf_match(&b, 0, "xx", 2));
  CHECK(buf_match_ci(&b, 0, "EL", 2));
  CHECK(!buf_match_ci(&b, 0, "zz", 2));
  buf_seek(&b, 3);
  CHECK(buf_insert(&b, 3, ".", 1));
  CHECK(buf_at(&b, 3) == '.');
  s = buf_strdup(&b);
  CHECK(s && strcmp(s, "elX.Xlo!") == 0);
  free(s);

  undo_init(&u);
  CHECK(undo_push(&u, U_INSERT, 0, "a", 1, true));
  CHECK(undo_push(&u, U_INSERT, 1, "b", 1, true)); /* coalesce */
  CHECK(u.head == 1);
  CHECK(u.items[0].len == 2);

  CHECK(utf8_decode("A", 1, &cp) == 1 && cp == 'A');
  CHECK(utf8_encode(0x442, enc) == 2); /* Cyrillic т */
  /* reject overlong / bad cont */
  CHECK(utf8_decode("\xC0\x80", 2, &cp) == 1 && cp == 0xFFFD);
  CHECK(utf8_decode("\xE0\x80\x80", 3, &cp) == 1 && cp == 0xFFFD);
  CHECK(utf8_decode("\x80", 1, &cp) == 1 && cp == 0xFFFD);

  /* paste-sized insert must not coalesce into prior typing */
  undo_free(&u);
  undo_init(&u);
  CHECK(undo_push(&u, U_INSERT, 0, "hi", 2, true));
  CHECK(undo_push(&u, U_INSERT, 2, "!", 1, false));
  CHECK(u.head == 2);

  fd = mkstemp(tmp);
  CHECK(fd >= 0);
  if (fd >= 0) {
    close(fd);
    unlink(tmp);
    CHECK(buf_save(&b, tmp));
    /* overwrite existing — must succeed on second save */
    CHECK(buf_insert(&b, buf_len(&b), "Z", 1));
    CHECK(buf_save(&b, tmp));
    buf_free(&b);
    CHECK(buf_init(&b, 0));
    CHECK(buf_load(&b, tmp));
    s = buf_strdup(&b);
    CHECK(s && strcmp(s, "elX.Xlo!Z") == 0);
    free(s);
    unlink(tmp);
  }

  buf_free(&b);
  undo_free(&u);
  if (fail) {
    fprintf(stderr, "%d checks failed\n", fail);
    return 1;
  }
  puts("ok");
  return 0;
}
