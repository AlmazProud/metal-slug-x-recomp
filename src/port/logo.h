/* Logo de la screen de título del juego para el menú de inicio (Fase 6C). */
#ifndef LOGO_H
#define LOGO_H

#include <SDL3/SDL.h>

/* Carga el logo de `cache` (PNG en la carpeta del usuario) o, si no existe, lo genera: corre la
 * máquina en silencio hasta la screen de título, redibuja solo los sprites del logo y lo guarda.
 * La máquina vuelve exactamente al estado en que estaba. Devuelve 0 si hay logo. */
int logo_prepare(SDL_Renderer *r, const char *cache);
SDL_Texture *logo_texture(void);         /* NULL si no hay logo */
void logo_destroy(void);

#endif
