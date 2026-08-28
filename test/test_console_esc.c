/* Console CSI → PlatKey (needs real TTY for plat_create). */
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
  console_test_feed(p, (const unsigned char *)"\033[19~", 6);
  expect(console_test_last_key(p) == PK_BOOKMARK_SET, "ESC[19~ F8");
  console_test_feed(p, (const unsigned char *)"\033[1;19~", 8);
  expect(console_test_last_key(p) == PK_BOOKMARK_SET, "ESC[1;19~ F8");
  console_test_feed(p, (const unsigned char *)"\033[19u", 6);
  expect(console_test_last_key(p) == PK_BOOKMARK_SET, "ESC[19u F8");
  console_test_feed(p, (const unsigned char *)"\033[20~", 6);
  expect(console_test_last_key(p) == PK_BOOKMARK, "ESC[20~ F9");
  console_test_feed(p, (const unsigned char *)"\033m", 2);
  expect(console_test_last_key(p) == PK_BOOKMARK_SET, "Alt+M");
  console_test_feed(p, (const unsigned char *)"\033J", 2);
  expect(console_test_last_key(p) == PK_BOOKMARK, "Alt+J");
  console_test_feed(p, (const unsigned char *)"\033[27;5;13~", 11);
  expect(console_test_last_key(p) == PK_BOOKMARK_SET, "modifyOtherKeys Ctrl+Shift+Enter");
  console_test_feed(p, (const unsigned char *)"\033[27;4;13~", 11);
  expect(console_test_last_key(p) == PK_BOOKMARK, "modifyOtherKeys Ctrl+Enter");
  console_test_feed(p, (const unsigned char *)"\x02", 1);
  expect(console_test_last_key(p) == PK_BOOKMARK, "Ctrl+B");
  plat_destroy(p);
  if (fails) return 1;
  puts("console esc OK");
  return 0;
}
