/* mote demo — hello_mote.c */
#include <stdio.h>
#include <string.h>

#define GREETING "hello, mote"

typedef struct {
  const char *name;
  int level;
} Hero;

static void greet(const Hero *h) {
  /* compact C editor by SYFaren */
  printf("%s - %s (lvl %d)\n", GREETING, h->name, h->level);
}

int main(void) {
  Hero me = {"SYFaren", 42};
  char buf[64];

  greet(&me);
  snprintf(buf, sizeof buf, "path: %s", __FILE__);
  puts(buf);
  return 0;
}
