/* Frontend debug mode (Phase 6A): F3 shows the panel. */
#ifndef DEBUG_H
#define DEBUG_H

#include <SDL3/SDL.h>

enum { DEBUG_NONE, DEBUG_HANDLED, DEBUG_STEP };

void debug_enable(int active);
int debug_enabled(void);
/* Debug mode keys; DEBUG_STEP asks to run a single frame (while paused). */
int debug_key(const SDL_KeyboardEvent *e);
void debug_frame(void);                 /* after every emulated frame */
/* game: rectangle where the image was drawn; w, h: output size. */
void debug_draw(SDL_Renderer *r, SDL_FRect game, int w, int h, int turbo);

#endif
