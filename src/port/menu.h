/* Menús del port (Fase 6C): pantalla de inicio, pausa, configuración y extras. */
#ifndef MENU_H
#define MENU_H

#include <SDL3/SDL.h>

void menu_inicio(void);                  /* pantalla de inicio: el demo del juego sigue de fondo */
void menu_pausa(void);                   /* pausa: el juego se detiene */
int menu_abierto(void);
int menu_juego_corre(void);              /* 1 en la pantalla de inicio (el juego corre sin entradas) */
/* Navegación: 0 arriba, 1 abajo, 2 izquierda, 3 derecha, 4 elegir, 5 volver. */
void menu_navegar(int t);
void menu_dibujar(SDL_Renderer *r, int w, int h);

/* Configuración de controles en curso (la captura de teclas y botones la hace el menú). */
int menu_capturando(void);
void menu_capturar_tecla(SDL_Scancode sc);   /* Esc termina, Supr deja igual */
void menu_capturar_boton(SDL_GamepadButton b);

#endif
