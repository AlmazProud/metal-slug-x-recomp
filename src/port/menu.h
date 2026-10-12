/* Port menus (Phase 6C): start screen, pause, settings and extras. */
#ifndef MENU_H
#define MENU_H

#include <SDL3/SDL.h>

void menu_start(void);                  /* start screen: the game's demo keeps running behind */
void menu_pause(void);                   /* pause: the game stops */
int menu_open(void);
int menu_game_runs(void);              /* 1 on the start screen (the game runs without input) */
/* Navigation: 0 up, 1 down, 2 left, 3 right, 4 select, 5 back. */
void menu_navigate(int t);
void menu_draw(SDL_Renderer *r, int w, int h);

/* Controls setup in progress (the menu captures the keys and buttons). */
int menu_capturing(void);
void menu_capture_key(SDL_Scancode sc);   /* Esc finishes, Del keeps the current one */
void menu_capture_button(SDL_GamepadButton b);

#endif
