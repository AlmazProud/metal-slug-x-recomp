/* Fuente de los menús (Fase 6C): la fuente sombreada de 8x8 del propio juego, leída de la copia
 * del usuario (layer fix del cartucho, tiles 0x300 + código ASCII). */
#ifndef FONT_H
#define FONT_H

#include <SDL3/SDL.h>

enum { INK_SILVER, INK_GOLD, INK_GRAY, INK_GREEN, INK_COUNT };

int font_create(SDL_Renderer *r);       /* tras cargar las ROMs; 0 = bien */
void font_destroy(void);
/* Texto en (x, y) con píxeles de `scale` (cada carácter mide 8 x scale). */
void font_text(SDL_Renderer *r, float x, float y, float scale, int tinta, const char *s);
void font_centered(SDL_Renderer *r, float cx, float y, float scale, int tinta, const char *s);
float font_width(const char *s, float scale);
/* Escala que no pasa de `maxima` y deja el text dentro de `ancho`. */
float font_fit_scale(const char *s, float maxima, float ancho);
/* Escala de la interfaz: un lienzo virtual de 480 x 300 que se ajusta a la window. */
float font_factor(int w, int h);

#endif
