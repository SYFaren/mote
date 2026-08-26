/* mote core — config.h */
#ifndef MOTE_CONFIG_H
#define MOTE_CONFIG_H

#define MOTE_CFG_RECENT 8

typedef struct {
  int win_w, win_h;
  int theme_id, font_px;
  int wrap, show_ws, find_case, find_word;
  char recent[MOTE_CFG_RECENT][1024];
  int nrecent;
} MoteCfg;

void cfg_defaults(MoteCfg *c);
int cfg_load(MoteCfg *c);
int cfg_save(const MoteCfg *c);

#endif
