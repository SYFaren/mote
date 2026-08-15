/* mote — developer: SYFaren — simple key=value config */
#ifndef MOTE_CONFIG_H
#define MOTE_CONFIG_H

typedef struct {
  int win_w, win_h;
} MoteCfg;

void cfg_defaults(MoteCfg *c);
int cfg_load(MoteCfg *c);  /* 0 ok / missing, -1 error */
int cfg_save(const MoteCfg *c);

#endif
