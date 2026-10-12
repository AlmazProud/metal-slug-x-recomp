/* Menús del port (Fase 6C): screen de inicio, pausa, configuración y extras. */
#ifndef MENU_H
#define MENU_H

#include <SDL3/SDL.h>

void menu_start(void);                  /* screen de inicio: el demo del juego sigue de fondo */
void menu_pause(void);                   /* pausa: el juego se detiene */
int menu_open(void);
int menu_game_runs(void);              /* 1 en la screen de inicio (el juego corre sin inputs) */
/* Navegación: 0 arriba, 1 abajo, 2 izquierda, 3 derecha, 4 elegir, 5 volver. */
void menu_navigate(int t);
void menu_draw(SDL_Renderer *r, int w, int h);

/* Configuración de controles en curso (la captura de teclas y botones la hace el menú). */
int menu_capturing(void);
void menu_capture_key(SDL_Scancode sc);   /* Esc termina, Supr deja igual */
void menu_capture_button(SDL_GamepadButton b);

#endif
