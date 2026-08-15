/* mote/mote-x/windows — developer: SYFaren */
#ifndef MOTE_PLATFORM_H
#define MOTE_PLATFORM_H
#include <stddef.h>
#include <stdbool.h>
#include <stdint.h>

typedef enum {
  PE_NONE = 0, PE_QUIT, PE_EXPOSE, PE_KEY, PE_TEXT,
  PE_MOUSE_DOWN, PE_MOUSE_UP, PE_MOUSE_MOVE, PE_SCROLL
} PlatEventType;

typedef enum {
  PK_NONE = 0, PK_LEFT, PK_RIGHT, PK_UP, PK_DOWN, PK_HOME, PK_END,
  PK_PGUP, PK_PGDN, PK_BACKSPACE, PK_DELETE, PK_ENTER, PK_ESCAPE, PK_TAB,
  PK_F1, PK_SAVE, PK_OPEN, PK_QUIT, PK_UNDO, PK_REDO, PK_FIND,
  PK_CUT, PK_COPY, PK_PASTE, PK_SELALL, PK_HELP, PK_SAVEAS,
  PK_THEME, PK_GOTO, PK_REPLACE, PK_FINDNEXT,
  PK_WRAP, PK_DELLINE, PK_DUPLINE, PK_FINDCASE, PK_FINDWORD, PK_WS,
  PK_ZOOMIN, PK_ZOOMOUT, PK_ZOOMRESET, PK_NEXTDOC, PK_PREVDOC, PK_NEWDOC,
  PK_RELOAD, PK_READONLY, PK_EOL, PK_RECENT,
  PK_CLOSEDOC, PK_FINDPREV, PK_BRACKET
} PlatKey;

typedef struct {
  PlatEventType type;
  PlatKey key;
  bool shift, ctrl;
  char text[32];
  int text_len;
  int mx, my, wheel;
} PlatEvent;

typedef struct Plat Plat;

Plat *plat_create(const char *title, int w, int h);
void plat_destroy(Plat *p);
/* Block until an X event is ready (no busy spin → no flicker). */
void plat_wait(Plat *p);
bool plat_poll(Plat *p, PlatEvent *ev);
void plat_get_size(Plat *p, int *w, int *h);
int plat_font_w(Plat *p);
int plat_font_h(Plat *p);
void plat_set_font_px(Plat *p, int px);
int plat_font_px(Plat *p);
void plat_begin_frame(Plat *p);
void plat_clear(Plat *p, uint32_t rgb);
void plat_fill_rect(Plat *p, int x, int y, int w, int h, uint32_t rgb);
void plat_draw_text(Plat *p, int x, int y, const char *s, int n, uint32_t rgb);
void plat_end_frame(Plat *p);
void plat_set_title(Plat *p, const char *title);
/* OS caret if supported; return true → skip software caret draw. */
bool plat_set_caret(Plat *p, int x, int y, int h, bool on);
char *plat_clipboard_get(Plat *p, size_t *out_len);
bool plat_clipboard_set(Plat *p, const char *s, size_t n);

#endif
