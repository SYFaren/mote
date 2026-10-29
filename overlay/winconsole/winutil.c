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

/* ASCII the shared keymap expects for a virtual-key
   code; the editor handles printable text separately. */
static int vk_ascii(unsigned vk) {
  if (vk >= 'A' && vk <= 'Z') return (int)(vk - 'A' + 'a');
  if (vk >= '0' && vk <= '9') return (int)vk;
  switch (vk) {
  case VK_OEM_1: return ';';
  case VK_OEM_PLUS: return '=';
  case VK_OEM_COMMA: return ',';
  case VK_OEM_MINUS: return '-';
  case VK_OEM_PERIOD: return '.';
  case VK_OEM_2: return '/';
  case VK_OEM_3: return '`';
  case VK_OEM_4: return '[';
  case VK_OEM_5: return '\\';
  case VK_OEM_6: return ']';
  case VK_OEM_7: return '\'';
  default: return 0;
  }
}

PlatKey win_vk_key(unsigned vk, mote_bool ctrl, mote_bool shift, mote_bool alt) {
  int key = vk_ascii(vk);
  (void)shift;
  if (alt && ctrl) return PK_NONE; /* AltGr is text input, not a shortcut. */
  switch (vk) {
  case VK_LEFT: return PK_LEFT;
  case VK_RIGHT: return PK_RIGHT;
  case VK_UP: return PK_UP;
  case VK_DOWN: return PK_DOWN;
  case VK_HOME: return PK_HOME;
  case VK_END: return PK_END;
  case VK_PRIOR: return PK_PGUP;
  case VK_NEXT: return PK_PGDN;
  case VK_DELETE: return PK_DELETE;
  case VK_BACK: return PK_BACKSPACE;
  case VK_RETURN: return PK_ENTER;
  case VK_TAB: return PK_TAB;
  case VK_ESCAPE: return PK_ESCAPE;
  case VK_F1: return key_fn(1, ctrl, shift);
  case VK_F2: return key_fn(2, ctrl, shift);
  case VK_F3: return key_fn(3, ctrl, shift);
  case VK_F4: return key_fn(4, ctrl, shift);
  case VK_F5: return key_fn(5, ctrl, shift);
  case VK_F6: return key_fn(6, ctrl, shift);
  case VK_F7: return key_fn(7, ctrl, shift);
  case VK_F8: return key_fn(8, ctrl, shift);
  case VK_F9: return key_fn(9, ctrl, shift);
  case VK_F10: return key_fn(10, ctrl, shift);
  case VK_F11: return key_fn(11, ctrl, shift);
  case VK_F12: return key_fn(12, ctrl, shift);
  default: break;
  }
  if (!key) return PK_NONE;
  if (ctrl) return key_ctrl(key, shift);
  if (alt) return key_alt(key);
  return PK_NONE;
}
