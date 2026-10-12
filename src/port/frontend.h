/* Lo que el frontend con window (window.c) ofrece a los menús (menu.c) y al menú de sonido. */
#ifndef FRONTEND_H
#define FRONTEND_H

#include "config.h"

extern Config cfg;

void fe_notify(const char *fmt, ...);
void fe_save_config(void);
void fe_state_save(void);            /* en la slot cfg.slot */
void fe_state_load(void);
void fe_apply_video(void);             /* screen completa */
void fe_apply_volume(void);
void fe_play(void);                     /* moneda y START del jugador 1, como un jugador */
void fe_reset(void);                 /* reinicio de la máquina (conserva la SRAM) */
void fe_service(void);                  /* botón de test: menú de service del BIOS */
void fe_apply_blood(void);
void fe_quit(void);

#endif
