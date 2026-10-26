/* Menu fonts (ui_font.h).
 *
 * Each (font, pixel size) pair is packed once into an atlas texture: printable ASCII, Latin-1
 * and a few typographic signs. The pixel size is the logical size times the renderer's scale,
 * so the atlas is rebuilt when the window changes size; a small cache keeps the pairs in use.
 */
#include "ui_font.h"

#include <stdlib.h>
#include <string.h>

/* stb_truetype, third party: its unused functions are not warnings of ours. */
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-function"
#define STB_TRUETYPE_IMPLEMENTATION
#define STBTT_STATIC
#include "../../external/stb/stb_truetype.h"
#pragma GCC diagnostic pop

extern const unsigned char font_black_ops_one[], font_chakra_regular[], font_chakra_semibold[], font_chakra_bold[];

static const unsigned char *const DATA[UI_FONTS] = {font_black_ops_one, font_chakra_regular, font_chakra_semibold,
                                                    font_chakra_bold};
static stbtt_fontinfo info[UI_FONTS];
static float cap[UI_FONTS];                   /* cap height in em */
static int ready;
static SDL_Renderer *renderer;

/* Characters in the atlases: ASCII, Latin-1 and these. */
static const int EXTRA[] = {0x2013, 0x2014, 0x2018, 0x2019, 0x201C, 0x201D, 0x2022, 0x2026, 0x2039, 0x203A};
#define N_ASCII 95                            /* 0x20-0x7E */
#define N_LATIN 96                            /* 0xA0-0xFF */
#define N_EXTRA ((int)(sizeof EXTRA / sizeof EXTRA[0]))
#define N_CHARS (N_ASCII + N_LATIN + N_EXTRA)

typedef struct {
    int font, px;
    SDL_Texture *tex;
    int w, h;
    stbtt_packedchar pc[N_CHARS];
} Atlas;

#define CACHE 40
static Atlas *cache[CACHE];
static int n_cache;

static int char_index(int c);

/* Atlas index of a character, with a plain ASCII stand-in when the font does not have it. */
static int glyph(int font, int c)
{
    if (c >= 0x80 && !stbtt_FindGlyphIndex(&info[font], c)) {
        switch (c) {
        case 0x2039: c = '<'; break;
        case 0x203A: c = '>'; break;
        case 0x2013: case 0x2014: c = '-'; break;
        case 0x2018: case 0x2019: c = '\''; break;
        case 0x201C: case 0x201D: c = '"'; break;
        case 0x2022: c = '-'; break;
        case 0x2026: c = '.'; break;
        default: c = '?'; break;
        }
    }
    return char_index(c);
}

static int char_index(int c)
{
    if (c >= 0x20 && c <= 0x7E) return c - 0x20;
    if (c >= 0xA0 && c <= 0xFF) return N_ASCII + c - 0xA0;
    for (int i = 0; i < N_EXTRA; i++)
        if (EXTRA[i] == c) return N_ASCII + N_LATIN + i;
    return '?' - 0x20;
}

/* Next UTF-8 code point (an invalid byte counts as itself, Latin-1). */
static int next_char(const char **p)
{
    const unsigned char *s = (const unsigned char *)*p;
    int c = *s++, extra = c >= 0xF0 ? 3 : c >= 0xE0 ? 2 : c >= 0xC0 ? 1 : 0;
    if (extra) {
        int v = c & (0x3F >> extra);
        for (int i = 0; i < extra; i++) {
            if ((s[i] & 0xC0) != 0x80) { *p = (const char *)s; return c; }
            v = v << 6 | (s[i] & 0x3F);
        }
        s += extra;
        c = v;
    }
    *p = (const char *)s;
    return c;
}

int ui_font_init(SDL_Renderer *r)
{
    renderer = r;
    for (int f = 0; f < UI_FONTS; f++) {
        if (!stbtt_InitFont(&info[f], DATA[f], stbtt_GetFontOffsetForIndex(DATA[f], 0))) return -1;
        int x0, y0, x1, y1;
        stbtt_GetCodepointBox(&info[f], 'H', &x0, &y0, &x1, &y1);
        cap[f] = y1 * stbtt_ScaleForMappingEmToPixels(&info[f], 1);
    }
    ready = 1;
    return 0;
}

static void clear_cache(void)
{
    for (int i = 0; i < n_cache; i++) {
        if (cache[i]->tex) SDL_DestroyTexture(cache[i]->tex);
        free(cache[i]);
    }
    n_cache = 0;
}

void ui_font_destroy(void)
{
    clear_cache();
    ready = 0;
}

static Atlas *bake(int font, int px)
{
    Atlas *a = calloc(1, sizeof *a);
    if (!a) return NULL;
    a->font = font;
    a->px = px;
    stbtt_pack_range ranges[3] = {
        {(float)px, 0x20, NULL, N_ASCII, a->pc, 0, 0},
        {(float)px, 0xA0, NULL, N_LATIN, a->pc + N_ASCII, 0, 0},
        {(float)px, 0, (int *)EXTRA, N_EXTRA, a->pc + N_ASCII + N_LATIN, 0, 0},
    };
    for (int i = 0; i < 3; i++) ranges[i].font_size = STBTT_POINT_SIZE((float)px);   /* px = the em size */
    for (int side = 256; side <= 4096; side *= 2) {
        unsigned char *alpha = calloc((size_t)side * side, 1);
        if (!alpha) break;
        stbtt_pack_context ctx;
        int ok = stbtt_PackBegin(&ctx, alpha, side, side, 0, 1, NULL);
        if (ok) {
            ok = stbtt_PackFontRanges(&ctx, DATA[font], 0, ranges, 3);
            stbtt_PackEnd(&ctx);
        }
        if (ok) {
            uint32_t *argb = malloc((size_t)side * side * 4);
            if (argb) {
                for (size_t i = 0; i < (size_t)side * side; i++) argb[i] = (uint32_t)alpha[i] << 24 | 0xFFFFFFu;
                a->tex = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, side, side);
                if (a->tex) {
                    SDL_UpdateTexture(a->tex, NULL, argb, side * 4);
                    SDL_SetTextureBlendMode(a->tex, SDL_BLENDMODE_BLEND);
                    SDL_SetTextureScaleMode(a->tex, SDL_SCALEMODE_LINEAR);
                }
                free(argb);
            }
            free(alpha);
            a->w = a->h = side;
            break;
        }
        free(alpha);
    }
    if (!a->tex) { free(a); return NULL; }
    return a;
}

static float scale_now(void)
{
    float sx = 1, sy = 1;
    if (renderer) SDL_GetRenderScale(renderer, &sx, &sy);
    return sx > 0 ? sx : 1;
}

static Atlas *atlas(int font, float size, float k)
{
    int px = (int)SDL_roundf(size * k);
    if (px < 4) px = 4;
    if (px > 400) px = 400;
    for (int i = 0; i < n_cache; i++)
        if (cache[i]->font == font && cache[i]->px == px) return cache[i];
    if (n_cache == CACHE) clear_cache();
    Atlas *a = bake(font, px);
    if (a) cache[n_cache++] = a;
    return a;
}

float ui_text_width(int font, float size, float spacing, const char *s)
{
    if (!ready || !s) return 0;
    float k = scale_now();
    Atlas *a = atlas(font, size, k);
    if (!a) return 0;
    float w = 0;
    int n = 0;
    while (*s) { w += a->pc[glyph(font, next_char(&s))].xadvance; n++; }
    return w / k + spacing * (n > 0 ? n - 1 : 0);
}

float ui_cap_height(int font, float size) { return ready ? cap[font] * size : size * 0.7f; }

float ui_text(SDL_Renderer *r, int font, float size, float spacing, float x, float y_mid, SDL_Color c,
              const char *s)
{
    if (!ready || !s) return 0;
    renderer = r;
    float k = scale_now();
    Atlas *a = atlas(font, size, k);
    if (!a) return 0;
    SDL_SetTextureColorMod(a->tex, c.r, c.g, c.b);
    SDL_SetTextureAlphaMod(a->tex, c.a);
    /* In output pixels: the baseline so that the capitals are centred on y_mid. */
    float px = SDL_roundf(x * k), base = SDL_roundf((y_mid + cap[font] * size / 2) * k), x0 = px;
    while (*s) {
        stbtt_aligned_quad q;
        stbtt_GetPackedQuad(a->pc, a->w, a->h, glyph(font, next_char(&s)), &px, &base, &q, 1);
        if (q.x1 > q.x0) {
            SDL_FRect src = {q.s0 * a->w, q.t0 * a->h, (q.s1 - q.s0) * a->w, (q.t1 - q.t0) * a->h};
            SDL_FRect dst = {q.x0 / k, q.y0 / k, (q.x1 - q.x0) / k, (q.y1 - q.y0) / k};
            SDL_RenderTexture(r, a->tex, &src, &dst);
        }
        if (*s) px += spacing * k;
    }
    return (px - x0) / k;
}

float ui_text_centered(SDL_Renderer *r, int font, float size, float spacing, float cx, float y_mid,
                       SDL_Color c, const char *s)
{
    renderer = r;
    return ui_text(r, font, size, spacing, cx - ui_text_width(font, size, spacing, s) / 2, y_mid, c, s);
}

float ui_text_right(SDL_Renderer *r, int font, float size, float spacing, float right, float y_mid,
                    SDL_Color c, const char *s)
{
    renderer = r;
    return ui_text(r, font, size, spacing, right - ui_text_width(font, size, spacing, s), y_mid, c, s);
}
