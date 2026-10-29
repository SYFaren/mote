#ifndef MOTE_ANSI_H
#define MOTE_ANSI_H

typedef int mote_bool;
#define MOTE_TRUE 1
#define MOTE_FALSE 0

/* Fixed-width pixel/color value. unsigned long is 8 bytes on 64-bit Unix. */
#if defined(__STDC_VERSION__) && __STDC_VERSION__ >= 199901L
#include <stdint.h>
typedef uint32_t mote_u32;
typedef uint16_t mote_u16;
#else
typedef unsigned int mote_u32;
typedef unsigned short mote_u16;
#endif

#endif
