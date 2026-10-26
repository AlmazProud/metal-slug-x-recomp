/* Menu fonts: TrueType text (external/fonts, OFL) drawn with stb_truetype.
 *
 * Coordinates and sizes are in the renderer's logical units (after SDL_SetRenderScale): the
 * glyphs are rasterized at the output's real pixel size, so text stays sharp at any window size
 * (4K, 8K). Text is UTF-8 (Latin-1 plus a few typographic signs).
 */
#ifndef UI_FONT_H
#define UI_FONT_H

#include <SDL3/SDL.h>

enum { UI_STENCIL, UI_REGULAR, UI_SEMIBOLD, UI_BOLD, UI_FONTS };

int ui_font_init(SDL_Renderer *r);       /* 0 = ok */
void ui_font_destroy(void);              /* also frees the textures (before the renderer) */
/* Width of the text at `size` (the em size), with `spacing` added after each character. */
float ui_text_width(int font, float size, float spacing, const char *s);
/* Text whose capital letters are vertically centred on y_mid; returns its width. */
float ui_text(SDL_Renderer *r, int font, float size, float spacing, float x, float y_mid, SDL_Color c,
              const char *s);
/* Same, centred / right-aligned on x. */
float ui_text_centered(SDL_Renderer *r, int font, float size, float spacing, float cx, float y_mid,
                       SDL_Color c, const char *s);
float ui_text_right(SDL_Renderer *r, int font, float size, float spacing, float right, float y_mid,
                    SDL_Color c, const char *s);
/* Cap height of the font at `size` (to place text by its top). */
float ui_cap_height(int font, float size);

#endif
