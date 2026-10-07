#include "winutil.h"
#include "keymap.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>

wchar_t *win_wide(const char *utf8) {
  int n = MultiByteToWideChar(CP_UTF8, 0, utf8 ? utf8 : "", -1, NULL, 0);
  wchar_t *w = n > 0 ? (wchar_t *)malloc((size_t)n * sizeof(wchar_t)) : NULL;
  if (w && !MultiByteToWideChar(CP_UTF8, 0, utf8 ? utf8 : "", -1, w, n)) {
    free(w);
    w = NULL;
  }
  return w;
}

char *win_utf8(const wchar_t *w) {
  int n = WideCharToMultiByte(CP_UTF8, 0, w ? w : L"", -1, NULL, 0, NULL, NULL);
  char *s = n > 0 ? (char *)malloc((size_t)n) : NULL;
  if (s && !WideCharToMultiByte(CP_UTF8, 0, w ? w : L"", -1, s, n, NULL, NULL)) {
    free(s);
    s = NULL;
  }
  return s;
}

char **win_utf8_argv(int argc, wchar_t **wargv) {
  char **argv = (char **)calloc((size_t)argc + 1, sizeof(char *));
  int i;
  if (!argv) return NULL;
  for (i = 0; i < argc; i++) {
    argv[i] = win_utf8(wargv[i]);
    if (!argv[i]) argv[i] = (char *)"";
  }
  return argv;
}

/* ASCII the shared keymap expects for a virtual-key code. */
static int vk_char(unsigned vk) {
  if ((vk >= 'A' && vk <= 'Z') || (vk >= '0' && vk <= '9')) return (int)vk;
  switch (vk) {
  case VK_OEM_2: return '/';
  case VK_OEM_5: return '\\';
  case VK_OEM_6: return ']';
  case VK_OEM_PLUS: return '=';
  case VK_OEM_MINUS:
  case VK_SUBTRACT: return '-';
  case VK_ADD: return '+';
  case VK_NUMPAD0: return '0';
  case VK_TAB: return '\t';
  case VK_RETURN: return '\r';
  default: return 0;
  }
}

static PlatKey vk_nav(unsigned vk) {
  switch (vk) {
  case VK_LEFT: return PK_LEFT;
  case VK_RIGHT: return PK_RIGHT;
  case VK_UP: return PK_UP;
  case VK_DOWN: return PK_DOWN;
  case VK_HOME: return PK_HOME;
  case VK_END: return PK_END;
  case VK_PRIOR: return PK_PGUP;
  case VK_NEXT: return PK_PGDN;
  case VK_BACK: return PK_BACKSPACE;
  case VK_DELETE: return PK_DELETE;
  case VK_RETURN: return PK_ENTER;
  case VK_ESCAPE: return PK_ESCAPE;
  case VK_TAB: return PK_TAB;
  default: return PK_NONE;
  }
}

PlatKey win_vk_key(unsigned vk, mote_bool ctrl, mote_bool shift, mote_bool alt) {
  int ch = vk_char(vk);
  PlatKey k = PK_NONE;
  if (!(ctrl && alt) && ch) k = alt ? key_alt(ch) : ctrl ? key_ctrl(ch, shift) : PK_NONE;
  if (k == PK_NONE) k = vk_nav(vk);
  if (k == PK_NONE && vk >= VK_F1 && vk <= VK_F12)
    k = key_fn((int)(vk - VK_F1) + 1, ctrl, shift);
  return k;
}
