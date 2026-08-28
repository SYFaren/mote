/* mote overlay/win32 */
#include "platform.h"
#include "common.h"
#include "utf8.h"

#define WIN32_LEAN_AND_MEAN
#include <windows.h>

#include <stdlib.h>
#include <string.h>
#include <stdio.h>

struct Plat {
  HWND hwnd;
  HDC hdc_win;
  HDC hdc_mem;
  HBITMAP dib;
  HBITMAP old_bmp;
  HFONT font;
  HFONT old_font;
  int width, height, fw, fh, font_px;
  mote_bool quit;
  mote_bool focused;
  mote_bool caret_on;
  mote_bool caret_shown; /* our ShowCaret/HideCaret nest tracking */
  mote_bool font_owned;
  int caret_h;
  int wheel_acc;
  unsigned high_surr;
  char *clip_store;
  size_t clip_len;
  PlatEvent queue[64];
  int qh, qt;
  /* brush cache — avoid CreateSolidBrush per glyph highlight */
  mote_u32 br_rgb[24];
  HBRUSH br_h[24];
  int br_n;
};

static Plat *g_plat;

static void push_ev(Plat *p, const PlatEvent *e) {
  int n = (p->qh + 1) % 64;
  if (n == p->qt) return;
  p->queue[p->qh] = *e;
  p->qh = n;
}

static void map_vk(WPARAM vk, mote_bool ctrl, mote_bool shift, mote_bool alt, PlatEvent *ev) {
  ev->type = PE_KEY;
  ev->ctrl = ctrl;
  ev->shift = shift;
  ev->key = PK_NONE;
  if (alt && !ctrl) {
    if (vk == 'C') { ev->key = PK_FINDCASE; return; }
    if (vk == 'W') { ev->key = PK_FINDWORD; return; }
    if (vk == 'S') { ev->key = PK_SAVEAS; return; }
    if (vk == 'R') { ev->key = PK_READONLY; return; }
    if (vk == 'K') { ev->key = PK_DELLINE; return; }
    if (vk == 'E') { ev->key = PK_EOL; return; }
    if (vk == 'H') { ev->key = PK_HELP; return; }
    if (vk == 'N') { ev->key = PK_NEXTDOC; return; }
    if (vk == 'P') { ev->key = PK_PREVDOC; return; }
    if (vk == 'M') { ev->key = PK_BOOKMARK_SET; return; }
    if (vk == 'J') { ev->key = PK_BOOKMARK; return; }
  }
  if (ctrl) {
    switch (vk) {
    case 'S': ev->key = shift ? PK_SAVEAS : PK_SAVE; return;
    case 'O': ev->key = PK_OPEN; return;
    case 'Q': ev->key = PK_QUIT; return;
    case 'Z': ev->key = PK_UNDO; return;
    case 'Y': ev->key = PK_REDO; return;
    case 'F': ev->key = PK_FIND; return;
    case 'X': ev->key = PK_CUT; return;
    case 'C': ev->key = PK_COPY; return;
    case 'V': ev->key = PK_PASTE; return;
    case 'A': ev->key = PK_SELALL; return;
    case 'H': ev->key = PK_HELP; return;
    case 'T': ev->key = PK_THEME; return;
    case 'G': ev->key = PK_GOTO; return;
    case 'R': ev->key = shift ? PK_READONLY : PK_REPLACE; return;
    case 'W': ev->key = shift ? PK_CLOSEDOC : PK_WRAP; return;
    case 'D': ev->key = PK_DUPLINE; return;
    case 'K': if (shift) { ev->key = PK_DELLINE; return; } break;
    case 'N': ev->key = PK_NEWDOC; return;
    case 'M':
      if (shift) {
        ev->key = PK_BOOKMARK_SET;
        return;
      }
      ev->key = PK_BOOKMARK;
      return;
    case 'J':
      ev->key = PK_BOOKMARK;
      return;
    case 'P': ev->key = shift ? PK_BOOKMARK_SET : PK_QUICKOPEN; return;
    case 'E': ev->key = shift ? PK_EOL : PK_RECENT; return;
    case VK_OEM_2: ev->key = PK_COMMENT; return;
    case VK_OEM_6: ev->key = PK_BRACKET; return; /* ] */
    case VK_OEM_5: if (shift) { ev->key = PK_BRACKET; return; } break; /* \ */
    case VK_OEM_PLUS:
    case VK_ADD: ev->key = PK_ZOOMIN; return;
    case VK_OEM_MINUS:
    case VK_SUBTRACT: ev->key = PK_ZOOMOUT; return;
    case '0':
    case VK_NUMPAD0: ev->key = PK_ZOOMRESET; return;
    case VK_TAB: ev->key = shift ? PK_PREVDOC : PK_NEXTDOC; return;
    default: break;
    }
  }
  switch (vk) {
  case VK_LEFT: ev->key = PK_LEFT; break;
  case VK_RIGHT: ev->key = PK_RIGHT; break;
  case VK_UP: ev->key = PK_UP; break;
  case VK_DOWN: ev->key = PK_DOWN; break;
  case VK_HOME: ev->key = PK_HOME; break;
  case VK_END: ev->key = PK_END; break;
  case VK_PRIOR: ev->key = PK_PGUP; break;
  case VK_NEXT: ev->key = PK_PGDN; break;
  case VK_BACK: ev->key = PK_BACKSPACE; break;
  case VK_DELETE: ev->key = PK_DELETE; break;
  case VK_RETURN:
    if (ctrl && shift) {
      ev->key = PK_BOOKMARK_SET;
      return;
    }
    if (ctrl) {
      ev->key = PK_BOOKMARK;
      return;
    }
    ev->key = PK_ENTER;
    break;
  case VK_ESCAPE: ev->key = PK_ESCAPE; break;
  case VK_TAB: ev->key = PK_TAB; break;
  case VK_F1: ev->key = PK_F1; break;
  case VK_F3: ev->key = shift ? PK_FINDPREV : PK_FINDNEXT; break;
  case VK_F4: if (ctrl) { ev->key = PK_CLOSEDOC; return; } break;
  case VK_F5: ev->key = PK_RELOAD; break;
  case VK_F7: ev->key = PK_WS; break;
  default: ev->type = PE_NONE; break;
  }
}

static void remake_dib(Plat *p) {
  BITMAPINFO bmi;
  int w = p->width > 0 ? p->width : 1;
  int h = p->height > 0 ? p->height : 1;
  if (p->dib) {
    SelectObject(p->hdc_mem, p->old_bmp);
    DeleteObject(p->dib);
    p->dib = NULL;
  }
  memset(&bmi, 0, sizeof bmi);
  bmi.bmiHeader.biSize = sizeof(BITMAPINFOHEADER);
  bmi.bmiHeader.biWidth = w;
  bmi.bmiHeader.biHeight = -h;
  bmi.bmiHeader.biPlanes = 1;
  bmi.bmiHeader.biBitCount = 32;
  bmi.bmiHeader.biCompression = BI_RGB;
  p->dib = CreateDIBSection(p->hdc_mem, &bmi, DIB_RGB_COLORS, NULL, NULL, 0);
  if (p->dib) p->old_bmp = (HBITMAP)SelectObject(p->hdc_mem, p->dib);
}

static wchar_t *u8_wide(const char *s) {
  int n;
  wchar_t *w;
  if (!s) s = "";
  n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
  if (n <= 0) return NULL;
  w = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
  if (!w) return NULL;
  if (!MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n)) {
    free(w);
    return NULL;
  }
  return w;
}

static LRESULT CALLBACK wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp) {
  Plat *p = g_plat;
  PlatEvent ev;
  if (!p) return DefWindowProcW(hwnd, msg, wp, lp);
  memset(&ev, 0, sizeof ev);
  switch (msg) {
  case WM_CLOSE:
    ev.type = PE_QUIT;
    push_ev(p, &ev);
    return 0;
  case WM_DESTROY:
    p->quit = MOTE_TRUE;
    PostQuitMessage(0);
    return 0;
  case WM_SIZE: {
    int nw = LOWORD(lp);
    int nh = HIWORD(lp);
    if (nw < 1) nw = 1;
    if (nh < 1) nh = 1;
    if (nw > 16384) nw = 16384;
    if (nh > 16384) nh = 16384;
    p->width = nw;
    p->height = nh;
    remake_dib(p);
    ev.type = PE_EXPOSE;
    push_ev(p, &ev);
    return 0;
  }
  case WM_SETFOCUS:
    p->focused = MOTE_TRUE;
    CreateCaret(hwnd, NULL, 2, p->caret_h > 0 ? p->caret_h : p->fh);
    p->caret_shown = MOTE_FALSE; /* CreateCaret leaves caret hidden */
    return 0;
  case WM_KILLFOCUS:
    p->focused = MOTE_FALSE;
    DestroyCaret();
    p->caret_shown = MOTE_FALSE;
    return 0;
  case WM_ERASEBKGND:
    return 1; /* stop Windows flicker */
  case WM_PAINT: {
    PAINTSTRUCT ps;
    BeginPaint(hwnd, &ps);
    if (p->caret_shown) {
      HideCaret(hwnd);
      p->caret_shown = MOTE_FALSE;
    }
    if (p->hdc_mem && p->dib)
      BitBlt(ps.hdc, 0, 0, p->width, p->height, p->hdc_mem, 0, 0, SRCCOPY);
    if (p->focused && p->caret_on) {
      ShowCaret(hwnd);
      p->caret_shown = MOTE_TRUE;
    }
    EndPaint(hwnd, &ps);
    return 0;
  }
  case WM_SYSKEYDOWN:
  case WM_KEYDOWN: {
    mote_bool ctrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
    mote_bool shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    mote_bool alt = (GetKeyState(VK_MENU) & 0x8000) != 0;
    map_vk(wp, ctrl, shift, alt, &ev);
    if (ev.type == PE_KEY) push_ev(p, &ev);
    return 0;
  }
  case WM_CHAR: {
    mote_u32 cp;
    char out[4];
    int n;
    if (GetKeyState(VK_CONTROL) & 0x8000) return 0;
    if (wp >= 0xD800 && wp <= 0xDBFF) {
      p->high_surr = (unsigned)wp;
      return 0;
    }
    if (p->high_surr && wp >= 0xDC00 && wp <= 0xDFFF) {
      cp = 0x10000u + (((p->high_surr - 0xD800u) << 10) | ((unsigned)wp - 0xDC00u));
      p->high_surr = 0;
    } else {
      p->high_surr = 0;
      cp = (mote_u32)wp;
    }
    if (cp < 32 || cp == 127) return 0;
    n = utf8_encode(cp, out);
    if (n <= 0) return 0;
    ev.type = PE_TEXT;
    memcpy(ev.text, out, (size_t)n);
    ev.text_len = n;
    push_ev(p, &ev);
    return 0;
  }
  case WM_LBUTTONDOWN:
    SetCapture(hwnd);
    ev.type = PE_MOUSE_DOWN;
    ev.mx = (short)LOWORD(lp);
    ev.my = (short)HIWORD(lp);
    ev.shift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
    push_ev(p, &ev);
    return 0;
  case WM_LBUTTONUP:
    ReleaseCapture();
    ev.type = PE_MOUSE_UP;
    ev.mx = (short)LOWORD(lp);
    ev.my = (short)HIWORD(lp);
    push_ev(p, &ev);
    return 0;
  case WM_MOUSEMOVE:
    if (wp & MK_LBUTTON) {
      int last = (p->qh + 63) % 64;
      if (p->qt != p->qh && p->queue[last].type == PE_MOUSE_MOVE) {
        p->queue[last].mx = (short)LOWORD(lp);
        p->queue[last].my = (short)HIWORD(lp);
      } else {
        ev.type = PE_MOUSE_MOVE;
        ev.mx = (short)LOWORD(lp);
        ev.my = (short)HIWORD(lp);
        push_ev(p, &ev);
      }
    }
    return 0;
  case WM_MOUSEWHEEL: {
    UINT lines = 3;
    int delta = (short)HIWORD(wp);
    int dlines;
    SystemParametersInfoA(SPI_GETWHEELSCROLLLINES, 0, &lines, 0);
    if (lines == 0) return 0;
    if (lines == WHEEL_PAGESCROLL) lines = 10;
    p->wheel_acc += delta;
    dlines = p->wheel_acc * (int)lines / WHEEL_DELTA;
    if (dlines == 0) return 0;
    p->wheel_acc -= dlines * WHEEL_DELTA / (int)lines;
    ev.type = PE_SCROLL;
    ev.wheel = dlines; /* positive = scroll up (content moves down) */
    push_ev(p, &ev);
    return 0;
  }
  default:
    return DefWindowProcW(hwnd, msg, wp, lp);
  }
}

Plat *plat_create(const char *title, int w, int h) {
  WNDCLASSW wc;
  TEXTMETRICW tm;
  wchar_t *wtitle;
  Plat *p = (Plat *)calloc(1, sizeof(Plat));
  if (!p) return NULL;
  SetProcessDPIAware();
  g_plat = p;
  p->width = w;
  p->height = h;
  memset(&wc, 0, sizeof wc);
  wc.style = CS_OWNDC;
  wc.lpfnWndProc = wndproc;
  wc.hInstance = GetModuleHandleW(NULL);
  wc.lpszClassName = L"mote-x";
  wc.hCursor = LoadCursorW(NULL, MAKEINTRESOURCEW(32513)); /* IDC_IBEAM */
  RegisterClassW(&wc);
  wtitle = u8_wide(title);
  p->hwnd = CreateWindowExW(
      0, L"mote-x", wtitle ? wtitle : L"mote-x", WS_OVERLAPPEDWINDOW | WS_VISIBLE,
      CW_USEDEFAULT, CW_USEDEFAULT, w, h, NULL, NULL, wc.hInstance, NULL);
  free(wtitle);
  if (!p->hwnd) {
    free(p);
    g_plat = NULL;
    return NULL;
  }
  p->hdc_win = GetDC(p->hwnd);
  p->hdc_mem = CreateCompatibleDC(p->hdc_win);
  p->font_px = 15;
  /* Fixed font with Cyrillic coverage (stock SYSTEM_FIXED often lacks glyphs). */
  p->font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                        DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
  if (!p->font)
    p->font = CreateFontW(-15, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                          DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                          DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Courier New");
  if (p->font) {
    p->font_owned = MOTE_TRUE;
  } else {
    p->font = (HFONT)GetStockObject(SYSTEM_FIXED_FONT);
    p->font_owned = MOTE_FALSE;
  }
  p->old_font = (HFONT)SelectObject(p->hdc_mem, p->font);
  GetTextMetricsW(p->hdc_mem, &tm);
  p->fw = tm.tmAveCharWidth;
  p->fh = tm.tmHeight;
  p->caret_h = p->fh;
  remake_dib(p);
  return p;
}

void plat_destroy(Plat *p) {
  int i;
  if (!p) return;
  free(p->clip_store);
  for (i = 0; i < p->br_n; i++) DeleteObject(p->br_h[i]);
  if (p->dib) {
    SelectObject(p->hdc_mem, p->old_bmp);
    DeleteObject(p->dib);
  }
  if (p->hdc_mem) {
    SelectObject(p->hdc_mem, p->old_font);
    DeleteDC(p->hdc_mem);
  }
  if (p->font_owned && p->font) DeleteObject(p->font);
  if (p->hdc_win && p->hwnd) ReleaseDC(p->hwnd, p->hdc_win);
  if (p->hwnd) DestroyWindow(p->hwnd);
  if (g_plat == p) g_plat = NULL;
  free(p);
}

void plat_wait(Plat *p) {
  MSG msg;
  if (p->qt != p->qh) return;
  if (GetMessageW(&msg, NULL, 0, 0) > 0) {
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  } else {
    p->quit = MOTE_TRUE;
  }
}

void plat_get_size(Plat *p, int *w, int *h) {
  *w = p->width;
  *h = p->height;
}

int plat_font_w(Plat *p) { return p->fw > 0 ? p->fw : 8; }
int plat_font_h(Plat *p) { return p->fh > 0 ? p->fh : 16; }

void plat_set_font_px(Plat *p, int px) {
  TEXTMETRICW tm;
  HFONT nf;
  if (!p) return;
  if (px < 8) px = 8;
  if (px > 48) px = 48;
  nf = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                   DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                   DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Consolas");
  if (!nf)
    nf = CreateFontW(-px, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
                     DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
                     DEFAULT_QUALITY, FIXED_PITCH | FF_MODERN, L"Courier New");
  if (!nf) return;
  SelectObject(p->hdc_mem, nf);
  if (p->font_owned && p->font) DeleteObject(p->font);
  p->font = nf;
  p->font_owned = MOTE_TRUE;
  p->font_px = px;
  GetTextMetricsW(p->hdc_mem, &tm);
  p->fw = tm.tmAveCharWidth;
  p->fh = tm.tmHeight;
  /* Force caret recreate after font change (zoom). */
  p->caret_h = -1;
}

int plat_font_px(Plat *p) { return p && p->font_px > 0 ? p->font_px : 15; }

void plat_begin_frame(Plat *p) {
  SelectObject(p->hdc_mem, p->font);
}

static HBRUSH brush_for(Plat *p, mote_u32 rgb) {
  int i;
  HBRUSH br;
  for (i = 0; i < p->br_n; i++) {
    if (p->br_rgb[i] == rgb) return p->br_h[i];
  }
  br = CreateSolidBrush(RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255));
  if (!br) return GetStockObject(BLACK_BRUSH);
  if (p->br_n < 24) {
    p->br_rgb[p->br_n] = rgb;
    p->br_h[p->br_n] = br;
    p->br_n++;
    return br;
  }
  /* replace slot 0 */
  DeleteObject(p->br_h[0]);
  p->br_rgb[0] = rgb;
  p->br_h[0] = br;
  return br;
}

void plat_clear(Plat *p, mote_u32 rgb) {
  RECT r;
  if (!p->hdc_mem) return;
  r.left = 0;
  r.top = 0;
  r.right = p->width;
  r.bottom = p->height;
  FillRect(p->hdc_mem, &r, brush_for(p, rgb));
}

void plat_fill_rect(Plat *p, int x, int y, int w, int h, mote_u32 rgb) {
  RECT r;
  if (!p->hdc_mem || w <= 0 || h <= 0) return;
  r.left = x;
  r.top = y;
  r.right = x + w;
  r.bottom = y + h;
  FillRect(p->hdc_mem, &r, brush_for(p, rgb));
}

void plat_draw_text(Plat *p, int x, int y, const char *s, int n, mote_u32 rgb) {
  wchar_t stack[128];
  wchar_t *w;
  int wn;
  mote_u32 cp;
  int len;
  if (!p->hdc_mem || !s || n <= 0) return;
  SetBkMode(p->hdc_mem, TRANSPARENT);
  SetTextColor(p->hdc_mem,
               RGB((rgb >> 16) & 255, (rgb >> 8) & 255, rgb & 255));
  /* Fast path: single BMP codepoint (editor draws per glyph). */
  len = utf8_decode(s, (size_t)n, &cp);
  if (len == n && cp <= 0xFFFFu) {
    wchar_t one = (wchar_t)cp;
    TextOutW(p->hdc_mem, x, y, &one, 1);
    return;
  }
  wn = MultiByteToWideChar(CP_UTF8, 0, s, n, NULL, 0);
  if (wn <= 0) return;
  w = wn <= 128 ? stack : (wchar_t *)malloc((size_t)wn * sizeof(wchar_t));
  if (!w) return;
  MultiByteToWideChar(CP_UTF8, 0, s, n, w, wn);
  TextOutW(p->hdc_mem, x, y, w, wn);
  if (w != stack) free(w);
}

void plat_end_frame(Plat *p) {
  if (!p->hdc_win || !p->hdc_mem) return;
  if (p->caret_shown) {
    HideCaret(p->hwnd);
    p->caret_shown = MOTE_FALSE;
  }
  BitBlt(p->hdc_win, 0, 0, p->width, p->height, p->hdc_mem, 0, 0, SRCCOPY);
  if (p->focused && p->caret_on) {
    ShowCaret(p->hwnd);
    p->caret_shown = MOTE_TRUE;
  }
}

void plat_set_title(Plat *p, const char *title) {
  wchar_t *w = u8_wide(title);
  if (w) {
    SetWindowTextW(p->hwnd, w);
    free(w);
  }
}

mote_bool plat_set_caret(Plat *p, int x, int y, int h, mote_bool on) {
  if (h < 1) h = p->fh;
  if (h != p->caret_h) {
    p->caret_h = h;
    if (p->focused) {
      if (p->caret_shown) {
        HideCaret(p->hwnd);
        p->caret_shown = MOTE_FALSE;
      }
      DestroyCaret();
      CreateCaret(p->hwnd, NULL, 2, h);
      p->caret_shown = MOTE_FALSE;
    }
  }
  p->caret_on = on;
  if (!p->focused) return MOTE_TRUE;
  if (on) {
    SetCaretPos(x, y);
    if (!p->caret_shown) {
      ShowCaret(p->hwnd);
      p->caret_shown = MOTE_TRUE;
    }
  } else if (p->caret_shown) {
    HideCaret(p->hwnd);
    p->caret_shown = MOTE_FALSE;
  }
  return MOTE_TRUE; /* OS caret — skip software draw */
}

mote_bool plat_clipboard_set(Plat *p, const char *s, size_t n) {
  HGLOBAL h;
  wchar_t *d;
  int wlen;
  free(p->clip_store);
  p->clip_store = (char *)malloc(n + 1);
  if (p->clip_store) {
    memcpy(p->clip_store, s, n);
    p->clip_store[n] = 0;
    p->clip_len = n;
  }
  if (!OpenClipboard(p->hwnd)) return MOTE_FALSE;
  EmptyClipboard();
  wlen = MultiByteToWideChar(CP_UTF8, 0, s, (int)n, NULL, 0);
  if (wlen < 0) wlen = 0;
  h = GlobalAlloc(GMEM_MOVEABLE, (size_t)(wlen + 1) * sizeof(wchar_t));
  if (!h) {
    CloseClipboard();
    return MOTE_FALSE;
  }
  d = (wchar_t *)GlobalLock(h);
  if (wlen)
    MultiByteToWideChar(CP_UTF8, 0, s, (int)n, d, wlen);
  d[wlen] = 0;
  GlobalUnlock(h);
  SetClipboardData(CF_UNICODETEXT, h);
  CloseClipboard();
  return MOTE_TRUE;
}

char *plat_clipboard_get(Plat *p, size_t *out_len) {
  HANDLE h;
  wchar_t *ws;
  char *out, *ansi;
  int n;
  if (out_len) *out_len = 0;
  if (!OpenClipboard(p->hwnd)) {
    if (!p->clip_store) return NULL;
    out = (char *)malloc(p->clip_len + 1);
    if (out) {
      memcpy(out, p->clip_store, p->clip_len + 1);
      if (out_len) *out_len = p->clip_len;
    }
    return out;
  }
  h = GetClipboardData(CF_UNICODETEXT);
  if (h) {
    ws = (wchar_t *)GlobalLock(h);
    n = ws ? WideCharToMultiByte(CP_UTF8, 0, ws, -1, NULL, 0, NULL, NULL) : 0;
    if (n > 0 && (size_t)n > MOTE_MAX_FILE + 1) n = (int)MOTE_MAX_FILE + 1;
    out = n > 0 ? (char *)malloc((size_t)n) : NULL;
    if (out && ws) {
      WideCharToMultiByte(CP_UTF8, 0, ws, -1, out, n, NULL, NULL);
      if (out_len) *out_len = n > 0 ? (size_t)(n - 1) : 0; /* exclude NUL */
    }
    GlobalUnlock(h);
    CloseClipboard();
    return out;
  }
  /* fallback ANSI (old apps) */
  h = GetClipboardData(CF_TEXT);
  if (!h) {
    CloseClipboard();
    if (!p->clip_store) return NULL;
    out = (char *)malloc(p->clip_len + 1);
    if (out) {
      memcpy(out, p->clip_store, p->clip_len + 1);
      if (out_len) *out_len = p->clip_len;
    }
    return out;
  }
  ansi = (char *)GlobalLock(h);
  n = ansi ? MultiByteToWideChar(CP_ACP, 0, ansi, -1, NULL, 0) : 0;
  if (n > 0) {
    wchar_t *tmp = (wchar_t *)malloc((size_t)n * sizeof(wchar_t));
    if (tmp) {
      MultiByteToWideChar(CP_ACP, 0, ansi, -1, tmp, n);
      n = WideCharToMultiByte(CP_UTF8, 0, tmp, -1, NULL, 0, NULL, NULL);
      if (n > 0 && (size_t)n > MOTE_MAX_FILE + 1) n = (int)MOTE_MAX_FILE + 1;
      out = n > 0 ? (char *)malloc((size_t)n) : NULL;
      if (out) {
        WideCharToMultiByte(CP_UTF8, 0, tmp, -1, out, n, NULL, NULL);
        if (out_len) *out_len = n > 0 ? (size_t)(n - 1) : 0;
      }
      free(tmp);
    } else
      out = NULL;
  } else
    out = NULL;
  GlobalUnlock(h);
  CloseClipboard();
  return out;
}

mote_bool plat_poll(Plat *p, PlatEvent *ev) {
  MSG msg;
  memset(ev, 0, sizeof *ev);
  while (PeekMessageW(&msg, NULL, 0, 0, PM_REMOVE)) {
    if (msg.message == WM_QUIT) {
      p->quit = MOTE_TRUE;
      ev->type = PE_QUIT;
      return MOTE_TRUE;
    }
    TranslateMessage(&msg);
    DispatchMessageW(&msg);
  }
  if (p->qt != p->qh) {
    *ev = p->queue[p->qt];
    p->qt = (p->qt + 1) % 64;
    return MOTE_TRUE;
  }
  if (p->quit) {
    ev->type = PE_QUIT;
    return MOTE_TRUE;
  }
  return MOTE_FALSE;
}
