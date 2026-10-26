/* Simple menu text (font.h) on top of the menu fonts (ui_font.c): Chakra Petch SemiBold in
 * uppercase, with a soft shadow, one color per ink. Used by the sound menu and the notices; the
 * main menus draw with ui_font.c directly.
 */
#include "font.h"

#include <string.h>

#include "ui_font.h"

static const SDL_Color INKS[INK_COUNT] = {
    {0xEC, 0xE6, 0xD8, 0xFF},                 /* silver: the menu's text */
    {0xF2, 0xA9, 0x00, 0xFF},                 /* gold: the accent */
    {0x8A, 0x92, 0x98, 0xFF},                 /* gray (disabled) */
    {0x9C, 0xCB, 0x6B, 0xFF},                 /* green (playing) */
};

int font_create(SDL_Renderer *r) { return ui_font_init(r); }
void font_destroy(void) { ui_font_destroy(); }

/* Uppercase copy (ASCII and Latin-1 in UTF-8). */
const char *font_upper(const char *s, char *b, size_t n)
{
    size_t i = 0;
    for (; s[i] && i + 1 < n; i++) {
        unsigned char c = (unsigned char)s[i];
        if (c >= 'a' && c <= 'z') c -= 0x20;
        /* Latin-1 lowercase U+00E0-U+00FE (not U+00F7): 0xC3 0xA0-0xBE in UTF-8 */
        else if (i > 0 && (unsigned char)s[i - 1] == 0xC3 && c >= 0xA0 && c <= 0xBE && c != 0xB7) c -= 0x20;
        b[i] = (char)c;
    }
    b[i] = 0;
    return b;
}

/* The em size whose capitals are 7 units tall per scale step (8 x 8 cells before). */
static float em(float scale) { return 7 * scale / ui_cap_height(UI_SEMIBOLD, 1); }

float font_width(const char *s, float scale)
{
    char b[512];
    return ui_text_width(UI_SEMIBOLD, em(scale), 0.5f * scale, font_upper(s, b, sizeof b));
}

float font_fit_scale(const char *s, float max_scale, float width)
{
    float e = width / font_width(s, 1);
    return e < max_scale ? SDL_floorf(e * 2) / 2 : max_scale;      /* in half steps */
}

float font_factor(int w, int h)
{
    float k = SDL_min(w / 480.0f, h / 300.0f);
    return k < 0.5f ? 0.5f : k;
}

void font_text(SDL_Renderer *r, float x, float y, float scale, int ink, const char *s)
{
    char b[512];
    font_upper(s, b, sizeof b);
    float mid = y + 4 * scale;
    ui_text(r, UI_SEMIBOLD, em(scale), 0.5f * scale, x + 0.6f * scale, mid + 0.6f * scale, (SDL_Color){0, 0, 0, 170}, b);
    ui_text(r, UI_SEMIBOLD, em(scale), 0.5f * scale, x, mid, INKS[ink], b);
}

void font_centered(SDL_Renderer *r, float cx, float y, float scale, int ink, const char *s)
{
    font_text(r, SDL_roundf(cx - font_width(s, scale) / 2), y, scale, ink, s);
}
