/* Simple menu text (sound menu, notices): uppercase text on the menu fonts (ui_font.h), sized
 * in steps of the old 8 x 8 cells so the layouts built on them keep working. */
#ifndef FONT_H
#define FONT_H

#include <SDL3/SDL.h>

enum { INK_SILVER, INK_GOLD, INK_GRAY, INK_GREEN, INK_COUNT };

int font_create(SDL_Renderer *r);       /* 0 = ok */
void font_destroy(void);
/* Text with its top at (x, y), capitals about 8 x scale tall. */
void font_text(SDL_Renderer *r, float x, float y, float scale, int ink, const char *s);
void font_centered(SDL_Renderer *r, float cx, float y, float scale, int ink, const char *s);
float font_width(const char *s, float scale);
/* Scale no larger than `max_scale` that keeps the text within `width`. */
float font_fit_scale(const char *s, float max_scale, float width);
/* UI scale: a 480 x 300 virtual canvas fitted to the window. */
float font_factor(int w, int h);
/* Uppercase copy of s in b (ASCII and Latin-1 letters, UTF-8). */
const char *font_upper(const char *s, char *b, size_t n);

#endif
