/* Fuente de los menús (Fase 6C): la fuente sombreada de 8x8 del propio juego, leída de la copia
 * del usuario (capa fix del cartucho, tiles 0x300 + código ASCII). */
#ifndef FUENTE_H
#define FUENTE_H

#include <SDL3/SDL.h>

enum { TINTA_PLATA, TINTA_ORO, TINTA_GRIS, TINTA_VERDE, TINTA_N };

int fuente_crear(SDL_Renderer *r);       /* tras cargar las ROMs; 0 = bien */
void fuente_destruir(void);
/* Texto en (x, y) con píxeles de `escala` (cada carácter mide 8 x escala). */
void fuente_texto(SDL_Renderer *r, float x, float y, float escala, int tinta, const char *s);
void fuente_centrado(SDL_Renderer *r, float cx, float y, float escala, int tinta, const char *s);
float fuente_ancho(const char *s, float escala);
/* Escala que no pasa de `maxima` y deja el texto dentro de `ancho`. */
float fuente_escala_ajustada(const char *s, float maxima, float ancho);
/* Escala de la interfaz: un lienzo virtual de 480 x 300 que se ajusta a la ventana. */
float fuente_factor(int w, int h);

#endif
