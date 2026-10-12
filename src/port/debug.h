/* Modo debug del frontend (Fase 6A): F3 muestra el panel. */
#ifndef DEBUG_H
#define DEBUG_H

#include <SDL3/SDL.h>

enum { DEBUG_NONE, DEBUG_HANDLED, DEBUG_STEP };

void debug_enable(int active);
int debug_enabled(void);
/* Teclas del mode debug; DEBUG_STEP pide ejecutar un solo cuadro (en pausa). */
int debug_key(const SDL_KeyboardEvent *e);
void debug_frame(void);                 /* después de cada cuadro emulado */
/* juego: rectángulo donde se dibujó la imagen; w, h: tamaño de la output. */
void debug_draw(SDL_Renderer *r, SDL_FRect juego, int w, int h, int turbo);

#endif
