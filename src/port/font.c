/* Menu font: tiles 0x300-0x37F of the cartridge fix layer (8x8 shaded font in ASCII order, the
 * one used for "CREDIT"). Indices: 1 outline, 2-5 gradient from bottom to top, 7 shadow,
 * 15 highlight; 0 transparent. The colors are the port's own (not the game's), one per ink.
 * A 16 x 6 character atlas per ink is built in one texture. The lowercase letters of this font
 * are very short and hard to tell apart: text is drawn in uppercase, as in the game.
 */
#include "font.h"

#include <string.h>

#include "neogeo.h"

#define BASE 0x300
static SDL_Texture *atlas;

/* [ink][index] ARGB; indices unused by the font take the middle tone. */
static const uint32_t INKS[INK_COUNT][6] = {
    /* outline, gradient 2-5, highlight */
    {0xFF000000, 0xFF707AA4, 0xFF929DCA, 0xFFB6C0EC, 0xFFE8F0FF},     /* silver */
    {0xFF000000, 0xFFA04400, 0xFFD87A10, 0xFFF8B232, 0xFFFFE47A},     /* gold */
    {0xFF000000, 0xFF4A4A4A, 0xFF606060, 0xFF7A7A7A, 0xFF9A9A9A},     /* gray (disabled) */
    {0xFF000000, 0xFF1E7A2A, 0xFF34A83E, 0xFF5CD064, 0xFFB2F7B0},     /* green (playing) */
};

static uint32_t color(int ink, int v)
{
    const uint32_t *t = INKS[ink];
    switch (v) {
    case 0: return 0;
    case 1: return t[0];
    case 2: case 3: case 4: case 5: return t[v - 1];
    case 7: return 0xFF181818;
    case 15: return 0xFFFFFFFF;
    default: return t[2];
    }
}

int font_create(SDL_Renderer *r)
{
    if (!ng_roms.fix_game) return -1;
    static uint32_t px[128 * 48 * INK_COUNT];
    for (int ink = 0; ink < INK_COUNT; ink++)
        for (int c = 0; c < 96; c++) {
            const uint8_t *t = ng_roms.fix_game + (size_t)(BASE + 0x20 + c) * 32;
            int ox = (c % 16) * 8, oy = ink * 48 + (c / 16) * 8;
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    uint8_t b = t[y * 4 + x / 2];
                    px[(oy + y) * 128 + ox + x] = color(ink, (x & 1) ? b >> 4 : b & 0x0F);
                }
        }
    atlas = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 128, 48 * INK_COUNT);
    if (!atlas) return -1;
    SDL_UpdateTexture(atlas, NULL, px, 128 * 4);
    SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(atlas, SDL_SCALEMODE_PIXELART);   /* even with non-integer scales */
    return 0;
}

void font_destroy(void)
{
    if (atlas) SDL_DestroyTexture(atlas);
    atlas = NULL;
}

float font_width(const char *s, float scale) { return (float)strlen(s) * 8 * scale; }

float font_fit_scale(const char *s, float max_scale, float width)
{
    float e = width / font_width(s, 1);
    return e < max_scale ? SDL_floorf(e * 2) / 2 : max_scale;      /* in half pixels */
}

float font_factor(int w, int h)
{
    float k = SDL_min(w / 480.0f, h / 300.0f);
    return k < 0.5f ? 0.5f : k;
}

void font_text(SDL_Renderer *r, float x, float y, float scale, int ink, const char *s)
{
    if (!atlas) { SDL_RenderDebugText(r, x, y, s); return; }
    for (; *s; s++, x += 8 * scale) {
        int c = (unsigned char)*s;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (c < 0x20 || c > 0x7F) c = '?';
        c -= 0x20;
        if (!c) continue;
        SDL_FRect o = {(float)(c % 16) * 8, (float)(ink * 48 + (c / 16) * 8), 8, 8};
        SDL_FRect d = {x, y, 8 * scale, 8 * scale};
        SDL_RenderTexture(r, atlas, &o, &d);
    }
}

void font_centered(SDL_Renderer *r, float cx, float y, float scale, int ink, const char *s)
{
    font_text(r, SDL_roundf(cx - font_width(s, scale) / 2), y, scale, ink, s);
}
