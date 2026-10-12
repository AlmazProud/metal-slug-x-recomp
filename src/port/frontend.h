/* Lo que el frontend con ventana (window.c) ofrece a los menús (menu.c) y al menú de sonido. */
#ifndef FRONTEND_H
#define FRONTEND_H

#include "config.h"

extern Config cfg;

void fe_avisar(const char *fmt, ...);
void fe_guardar_config(void);
void fe_estado_guardar(void);            /* en la ranura cfg.ranura */
void fe_estado_cargar(void);
void fe_aplicar_video(void);             /* pantalla completa */
void fe_aplicar_volumen(void);
void fe_jugar(void);                     /* moneda y START del jugador 1, como un jugador */
void fe_reiniciar(void);                 /* reinicio de la máquina (conserva la SRAM) */
void fe_servicio(void);                  /* botón de test: menú de servicio del BIOS */
void fe_aplicar_sangre(void);
void fe_salir(void);

#endif
