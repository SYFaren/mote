/* mote core — theme.c */
#include "theme.h"

static const Theme themes[] = {
    {
        "dark",
        0x1E1E1E, 0xD4D4D4, 0x264F78, 0x2A2A2A,
        0x181818, 0x858585,
        0x0E639C, 0xFFFFFF, 0xAEAFAD,
        0x2D2D30, 0x007ACC,
        0x569CD6, 0x4EC9B0, 0xCE9178, 0x6A9955,
        0xB5CEA8, 0xC586C0, 0x613214, 0xFFD700,
    },
    {
        "light",
        0xFFFFFF, 0x1E1E1E, 0xADD6FF, 0xF3F3F3,
        0xF0F0F0, 0x6E6E6E,
        0x007ACC, 0xFFFFFF, 0x000000,
        0xF3F3F3, 0x007ACC,
        0x0000FF, 0x267F99, 0xA31515, 0x008000,
        0x098658, 0xAF00DB, 0xFFE08A, 0xCD7C00,
    },
    {
        "slate",
        0x0F1419, 0xE6E1CF, 0x253340, 0x151A1E,
        0x0A0E12, 0x5C6773,
        0x39BAE6, 0x0F1419, 0xE6E1CF,
        0x1A1F24, 0xFF8F40,
        0xFF8F40, 0x7FD962, 0xFFD173, 0x5C6773,
        0xF29E74, 0xD4BFFF, 0x3D2B00, 0xFFCC66,
    },
};

int theme_count(void) { return (int)(sizeof themes / sizeof themes[0]); }

const Theme *theme_get(int id) {
  int n = theme_count();
  if (id < 0) id = 0;
  if (id >= n) id = id % n;
  return &themes[id];
}

const char *theme_name(int id) { return theme_get(id)->name; }
