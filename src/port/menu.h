/* Port menus: start screen, pause, game modes, settings and extras. */
#ifndef MENU_H
#define MENU_H

#include <SDL3/SDL.h>

void menu_start(void);                  /* start screen: the game's demo keeps running behind */
void menu_pause(void);                   /* pause: the game stops */
int menu_open(void);
int menu_game_runs(void);              /* 1 on the start screen (the game runs without input) */
/* Navigation: 0 up, 1 down, 2 left, 3 right, 4 select, 5 back, 6 clear (the controls' rows),
   9 previous tab, 10 next tab. */
void menu_navigate(int t);
void menu_draw(SDL_Renderer *r, int w, int h);
/* The last input came from a gamepad (1) or the keyboard (0): the key hints follow it. */
void menu_set_gamepad(int on);
/* Mouse, in the renderer's output pixels: moving over an option selects it, a click does what
   Enter does (or changes the value on its arrows), the wheel moves the selection. */
void menu_mouse_move(float x, float y);
void menu_mouse_click(float x, float y);
void menu_mouse_wheel(float dy);

/* A control being changed (Settings > Keyboard / Gamepad): the menu captures the next key or
   gamepad input (PAD_*: a button or a trigger). */
int menu_capturing(void);
void menu_capture_key(SDL_Scancode sc);   /* Esc cancels (except on the menu's own row) */
void menu_capture_button(int in);

/* Arcade mode: the game is over, the results page opens (window.c). */
void menu_arcade_results(void);

#endif
