/* mote — buffer / utf8 / undo / hl self-test — SYFaren */
#define _POSIX_C_SOURCE 200809L
#include "buffer.h"
#include "utf8.h"
#include "undo.h"
#include "hl.h"
#include "regex.h"
#include "mote_snprintf.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <unistd.h>

static int fail;

static Buf rb;

static size_t re(const char *text, size_t pos, const char *pat, mote_bool ci) {
  buf_free(&rb);
  if (!buf_init(&rb, 0) || !buf_insert(&rb, 0, text, strlen(text))) return 0;
  return re_match_buf(&rb, pos, pat, ci);
}

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
  mote_u32 cp;
  char enc[4];
  const HlSyntax *syn;
  HlSpan sp[HL_MAX_SPANS];
  char fb[8];
  int ml, ns;

  CHECK(sizeof(mote_u32) == 4);
  CHECK(sizeof(mote_u16) == 2);

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
  CHECK(undo_push(&u, U_INSERT, 0, "a", 1, MOTE_TRUE));
  CHECK(undo_push(&u, U_INSERT, 1, "b", 1, MOTE_TRUE));
  CHECK(u.head == 1);
  CHECK(u.items[0].len == 2);

  CHECK(utf8_decode("A", 1, &cp) == 1 && cp == 'A');
  CHECK(utf8_encode(0x442, enc) == 2);
  CHECK(utf8_decode("\xC0\x80", 2, &cp) == 1 && cp == 0xFFFD);
  CHECK(utf8_decode("\xE0\x80\x80", 3, &cp) == 1 && cp == 0xFFFD);
  CHECK(utf8_decode("\x80", 1, &cp) == 1 && cp == 0xFFFD);

  undo_free(&u);
  undo_init(&u);
  CHECK(undo_push(&u, U_INSERT, 0, "hi", 2, MOTE_TRUE));
  CHECK(undo_push(&u, U_INSERT, 2, "!", 1, MOTE_FALSE));
  CHECK(u.head == 2);

  /* DOS folds .c → .C — HL must still select C syntax */
  syn = hl_select("hello.c");
  CHECK(syn != NULL && strcmp(hl_lang_name(syn), "c/c++") == 0);
  syn = hl_select("HELLO.C");
  CHECK(syn != NULL && strcmp(hl_lang_name(syn), "c/c++") == 0);
  syn = hl_select("C:\\SRC\\FOO.C");
  CHECK(syn != NULL);
  syn = hl_select("readme.txt");
  CHECK(syn == NULL);

  /* regex: case flag, classes, line anchors */
  CHECK(re("Hello", 0, "hel+o", MOTE_TRUE) == 5);
  CHECK(re("Hello", 0, "hel+o", MOTE_FALSE) == 0);
  CHECK(re("Hello", 0, "[a-h]ello", MOTE_TRUE) == 5);
  CHECK(re("hello", 0, "[A-H]ello", MOTE_TRUE) == 5);
  CHECK(re("hello", 0, "[A-H]ello", MOTE_FALSE) == 0);
  CHECK(re("ab\ncd", 0, "ab$", MOTE_FALSE) == 2);
  CHECK(re("ab\ncd", 3, "^cd", MOTE_FALSE) == 2);
  CHECK(re("ab\ncd", 1, "^b", MOTE_FALSE) == 0);
  buf_free(&rb);

  /* mote_snprintf: C99 return value (full length) and truncation */
  CHECK(mote_snprintf(fb, sizeof fb, "%d|%s", -42, "abc") == 7);
  CHECK(strcmp(fb, "-42|abc") == 0);
  CHECK(mote_snprintf(fb, sizeof fb, "%s", "0123456789") == 10);
  CHECK(strcmp(fb, "0123456") == 0);
  CHECK(mote_snprintf(fb, sizeof fb, "%04x%-3u|", 0xabu, 7u) == 8);
  CHECK(strcmp(fb, "00ab7  ") == 0);
  CHECK(mote_snprintf(fb, sizeof fb, "%.*s%c", 2, "xyz", '!') == 3);
  CHECK(strcmp(fb, "xy!") == 0);
  CHECK(mote_snprintf(fb, sizeof fb, "%05d", -7) == 5 && strcmp(fb, "-0007") == 0);

  /* markdown: two bold runs on one line, nothing highlighted between them */
  syn = hl_select("x.md");
  ns = hl_line(syn, "**a** b **c**", 13, 0, sp, HL_MAX_SPANS, &ml);
  CHECK(ns == 2 && sp[0].start == 0 && sp[0].len == 5 && sp[1].start == 8 &&
        sp[1].len == 5);

  fd = mkstemp(tmp);
  CHECK(fd >= 0);
  if (fd >= 0) {
    close(fd);
    unlink(tmp);
    CHECK(buf_save(&b, tmp));
    CHECK(buf_insert(&b, buf_len(&b), "Z", 1));
    CHECK(buf_save(&b, tmp));
    buf_free(&b);
    CHECK(buf_init(&b, 0));
    CHECK(buf_load(&b, tmp));
    s = buf_strdup(&b);
    CHECK(s && strcmp(s, "elX.Xlo!Z") == 0);
    free(s);
    /* a real U+FFFD is valid UTF-8 and must not trigger CP1251 conversion */
    buf_free(&b);
    CHECK(buf_init(&b, 0) && buf_insert(&b, 0, "\xEF\xBF\xBD", 3));
    CHECK(buf_save(&b, tmp));
    buf_free(&b);
    CHECK(buf_init(&b, 0) && buf_load(&b, tmp));
    CHECK(buf_len(&b) == 3 && buf_at(&b, 0) == '\xEF');
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
