/* Menu font (Phase 6C): the game's own 8x8 shaded font, read from the user's copy (cartridge
 * fix layer, tiles 0x300 + ASCII code). */
#ifndef FONT_H
#define FONT_H

#include <SDL3/SDL.h>

enum { INK_SILVER, INK_GOLD, INK_GRAY, INK_GREEN, INK_COUNT };

int font_create(SDL_Renderer *r);       /* after loading the ROMs; 0 = ok */
void font_destroy(void);
/* Text at (x, y) with pixels of size `scale` (each character is 8 x scale). */
void font_text(SDL_Renderer *r, float x, float y, float scale, int ink, const char *s);
void font_centered(SDL_Renderer *r, float cx, float y, float scale, int ink, const char *s);
float font_width(const char *s, float scale);
/* Scale no larger than `max_scale` that keeps the text within `width`. */
float font_fit_scale(const char *s, float max_scale, float width);
/* UI scale: a 480 x 300 virtual canvas fitted to the window. */
float font_factor(int w, int h);

#endif
