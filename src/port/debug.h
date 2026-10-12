/* Modo debug del frontend (Fase 6A): F3 muestra el panel. */
#ifndef DEBUG_H
#define DEBUG_H

#include <SDL3/SDL.h>

enum { DEBUG_NADA, DEBUG_USADA, DEBUG_AVANZAR };

void debug_activar(int activo);
int debug_activo(void);
/* Teclas del modo debug; DEBUG_AVANZAR pide ejecutar un solo cuadro (en pausa). */
int debug_tecla(const SDL_KeyboardEvent *e);
void debug_cuadro(void);                 /* después de cada cuadro emulado */
/* juego: rectángulo donde se dibujó la imagen; w, h: tamaño de la salida. */
void debug_dibujar(SDL_Renderer *r, SDL_FRect juego, int w, int h, int turbo);

#endif
