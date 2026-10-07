#include "platform.h"
#include "utf8.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

Plat *plat_create(const char *title, int w, int h);
void plat_destroy(Plat *p);
void console_test_feed(Plat *p, const unsigned char *buf, int n);
PlatKey console_test_last_key(const Plat *p);

static int fails;

static void expect(int ok, const char *msg) {
  if (!ok) {
    fprintf(stderr, "FAIL: %s\n", msg);
    fails++;
  }
}

static void feed_expect(Plat *p, const char *seq, PlatKey want, const char *what) {
  console_test_feed(p, (const unsigned char *)seq, (int)strlen(seq));
  expect(console_test_last_key(p) == want, what);
}

int main(void) {
  Plat *p;
  if (!isatty(STDIN_FILENO)) {
    puts("skip test_console_esc (no TTY)");
    return 0;
  }
  fails = 0;
  p = plat_create("esc", 80, 24);
  if (!p) {
    puts("skip test_console_esc (plat_create failed)");
    return 0;
  }
  feed_expect(p, "\033[19~", PK_BOOKMARK_SET, "F8");
  feed_expect(p, "\033[19;2~", PK_BOOKMARK_SET, "Shift+F8 (code first, modifier second)");
  feed_expect(p, "\033[20~", PK_BOOKMARK, "F9");
  feed_expect(p, "\033[13;2~", PK_FINDPREV, "Shift+F3 (vt220 style)");
  feed_expect(p, "\033[1;2R", PK_FINDPREV, "Shift+F3 (xterm)");
  feed_expect(p, "\033OR", PK_FINDNEXT, "F3 (SS3)");
  feed_expect(p, "\033[[A", PK_F1, "F1 (Linux VT)");
  feed_expect(p, "\033[1;5C", PK_RIGHT, "Ctrl+Right");
  feed_expect(p, "\033m", PK_BOOKMARK_SET, "Alt+M");
  feed_expect(p, "\033J", PK_BOOKMARK, "Alt+J");
  feed_expect(p, "\033[27;5;13~", PK_BOOKMARK, "modifyOtherKeys Ctrl+Enter");
  feed_expect(p, "\033[27;6;13~", PK_BOOKMARK_SET, "modifyOtherKeys Ctrl+Shift+Enter");
  feed_expect(p, "\033[109;5u", PK_BOOKMARK, "CSI u Ctrl+M");
  feed_expect(p, "\033[9;5u", PK_NEXTDOC, "CSI u Ctrl+Tab");
  feed_expect(p, "\x02", PK_BOOKMARK, "Ctrl+B");
  feed_expect(p, "\x1d", PK_BRACKET, "Ctrl+]");
  feed_expect(p, "\x1f", PK_COMMENT, "Ctrl+/");
  plat_destroy(p);
  if (fails) return 1;
  puts("console esc OK");
  return 0;
}
