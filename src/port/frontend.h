/* What the windowed frontend (window.c) provides to the menus (menu.c) and the sound menu. */
#ifndef FRONTEND_H
#define FRONTEND_H

#include "config.h"

extern Config cfg;

void fe_notify(const char *fmt, ...);
void fe_save_config(void);
void fe_state_save(void);            /* to slot cfg.slot */
void fe_state_load(void);
void fe_apply_video(void);             /* fullscreen */
void fe_apply_volume(void);
void fe_play(void);                     /* player 1 coin and START, like a player would */
void fe_reset(void);                 /* machine reset (keeps the SRAM) */
void fe_service(void);                  /* test button: BIOS service menu */
void fe_apply_blood(void);
void fe_dev_menu(void);                 /* the game's hidden developer menu (F4) */
void fe_quit(void);

#endif
