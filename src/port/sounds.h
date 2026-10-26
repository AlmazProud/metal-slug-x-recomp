/* Phase 6B/6C: music, effects and voices menu, and gamepad rumble. */
#ifndef SOUNDS_H
#define SOUNDS_H

#include <SDL3/SDL.h>
#include <stdint.h>

#include "profile.h"

/* Metal Slug sound driver (Metal Slug X; to be checked for each game): a mode command (00 music,
 * 10 effects, 11 voices) stays active and the next command is interpreted in that mode. 03 resets
 * the driver. The built-in names are the game profile's (game_profile.sounds). */
enum { MODE_MUSIC = 0x00, MODE_EFFECTS = 0x10, MODE_VOICES = 0x11 };


/* ------------------------------------------------------------------ sound menu */
/* window: for typing names; path: the user's sound names and rumble file (sounds.ini). */
void sounds_init(SDL_Window *window, const char *path);
void sounds_show(void);                /* saves the game state and stops the 68000 */
void sounds_close(void);               /* restores the game exactly as it was */
int sounds_open(void);
void sounds_frame(void);               /* advances the sound one frame (instead of the game) */
/* Navigation: 0 up, 1 down, 2 left, 3 right, 4 listen, 5 exit, 6 stop, 7 rename,
 * 8 change the sound's rumble. */
void sounds_navigate(int t);
void sounds_draw(SDL_Renderer *r, int w, int h);
/* Name being typed. */
int sounds_editing(void);
void sounds_text(const char *utf8);
void sounds_edit_key(SDL_Scancode sc);

/* ------------------------------------------------------------------ rumble */
/* Watches the game's sound commands; strength 0-100 (0 = off). */
void rumble_init(SDL_Gamepad **gamepads, int n);
void rumble_strength(int strength);
void rumble_frame(void);             /* applies what was requested during the frame */

#endif
