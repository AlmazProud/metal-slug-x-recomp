/* CRT effect (crt.h). */
#include "crt.h"

#include <math.h>
#include <stdint.h>

#include "neogeo.h"

typedef struct {
    float line_depth;            /* how dark the gap between two lines gets (0-1) */
    float beam;                  /* the beam's width (gaussian sigma, in line heights) */
    float mask;                  /* how much the mask takes from the other two colours (0-1) */
    int dots;                    /* 0 aperture grille, 1 slot mask */
    float give_back;             /* the image added again (0-1) */
    float glow;                  /* the blurred image added (bloom, 0-1) */
    float vignette;              /* darkening at the corners (0-1) */
    float curve;                 /* the screen's curvature (0 = flat) */
} Style;

static const Style STYLES[CRT_COUNT] = {
    [CRT_ARCADE] = {0.62f, 0.30f, 0.38f, 0, 0.30f, 0.22f, 0.18f, 0.025f},
    [CRT_TV] = {0.45f, 0.36f, 0.30f, 1, 0.24f, 0.30f, 0.32f, 0.050f},
    [CRT_SOFT] = {0.30f, 0.40f, 0.00f, 0, 0.12f, 0.20f, 0.12f, 0.030f},
};

static SDL_Texture *lines, *grille, *dots, *vignette, *glow_a, *glow_b, *screen;
static int lines_h, lines_style = -1, screen_w, screen_h;
static float lines_pitch;

void crt_destroy(void)
{
    SDL_Texture **t[7] = {&lines, &grille, &dots, &vignette, &glow_a, &glow_b, &screen};
    for (int i = 0; i < 7; i++)
        if (*t[i]) { SDL_DestroyTexture(*t[i]); *t[i] = NULL; }
    lines_style = -1;
    screen_w = screen_h = 0;
}

static SDL_Texture *texture(SDL_Renderer *r, int w, int h, const uint32_t *px, SDL_BlendMode blend, SDL_ScaleMode scale)
{
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, w, h);
    if (!t) return NULL;
    SDL_UpdateTexture(t, NULL, px, w * 4);
    SDL_SetTextureBlendMode(t, blend);
    SDL_SetTextureScaleMode(t, scale);
    return t;
}

static SDL_Texture *target(SDL_Renderer *r, int w, int h)
{
    SDL_Texture *t = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET, w, h);
    if (t) SDL_SetTextureScaleMode(t, SDL_SCALEMODE_LINEAR);
    return t;
}

static uint32_t grey(float v)
{
    int g = (int)(v * 255 + 0.5f);
    g = g < 0 ? 0 : g > 255 ? 255 : g;
    return 0xFF000000u | (uint32_t)g << 16 | (uint32_t)g << 8 | (uint32_t)g;
}

/* One column, a row per output pixel: the beam's profile across the game's 224 lines. */
static void make_lines(SDL_Renderer *r, int h, float pitch, const Style *s)
{
    if (lines) SDL_DestroyTexture(lines);
    uint32_t *px = SDL_malloc((size_t)h * 4);
    if (!px) { lines = NULL; return; }
    /* thin lines (small windows) would beat against the pixels: lighter there */
    float depth = s->line_depth * SDL_clamp((pitch - 1.5f) / 2.0f, 0.0f, 1.0f);
    for (int y = 0; y < h; y++) {
        float v = (y + 0.5f) / pitch, f = v - SDL_floorf(v) - 0.5f;      /* -0.5..0.5 from the centre */
        float beam = SDL_expf(-(f * f) / (2 * s->beam * s->beam));
        px[y] = grey(1 - depth * (1 - beam));
    }
    lines = texture(r, 1, h, px, SDL_BLENDMODE_MOD, SDL_SCALEMODE_NEAREST);
    SDL_free(px);
}

static SDL_Texture *make_mask(SDL_Renderer *r, int with_dots, float strength)
{
    uint8_t k = (uint8_t)((1 - strength) * 255);
    uint32_t R = 0xFF000000u | 0xFFu << 16 | (uint32_t)k << 8 | k;
    uint32_t G = 0xFF000000u | (uint32_t)k << 16 | 0xFFu << 8 | k;
    uint32_t B = 0xFF000000u | (uint32_t)k << 16 | (uint32_t)k << 8 | 0xFF;
    if (!with_dots) {
        const uint32_t px[3] = {R, G, B};
        return texture(r, 3, 1, px, SDL_BLENDMODE_MOD, SDL_SCALEMODE_NEAREST);
    }
    /* a TV's slot mask: RGB triads cut by dark gaps, staggered between neighbouring triads */
    uint32_t K = 0xFF000000u | (uint32_t)k << 16 | (uint32_t)k << 8 | k;
    const uint32_t px[24] = {
        K, K, K, R, G, B,
        R, G, B, R, G, B,
        R, G, B, K, K, K,
        R, G, B, R, G, B,
    };
    return texture(r, 6, 4, px, SDL_BLENDMODE_MOD, SDL_SCALEMODE_NEAREST);
}

static void make_vignette(SDL_Renderer *r)
{
    enum { N = 64 };
    static uint32_t px[N * N];
    for (int y = 0; y < N; y++)
        for (int x = 0; x < N; x++) {
            float u = (x + 0.5f) / N * 2 - 1, v = (y + 0.5f) / N * 2 - 1;
            float d = SDL_sqrtf(u * u * 0.8f + v * v);                 /* a bit wider than tall */
            float a = SDL_clamp((d - 0.55f) / 0.85f, 0.0f, 1.0f);
            px[y * N + x] = (uint32_t)(a * a * 255) << 24;            /* black, alpha */
        }
    vignette = texture(r, N, N, px, SDL_BLENDMODE_BLEND, SDL_SCALEMODE_LINEAR);
}

/* The glow: the image shrunk twice (each step blurs it), to be stretched back over it. */
static SDL_Texture *blurred(SDL_Renderer *r, SDL_Texture *base)
{
    if (!glow_a) glow_a = target(r, NG_WIDTH / 2, NG_HEIGHT / 2);
    if (!glow_b) glow_b = target(r, NG_WIDTH / 4, NG_HEIGHT / 4);
    if (!glow_a || !glow_b) return NULL;
    SDL_ScaleMode old;
    SDL_GetTextureScaleMode(base, &old);
    SDL_SetTextureScaleMode(base, SDL_SCALEMODE_LINEAR);
    SDL_SetRenderTarget(r, glow_a);
    SDL_RenderTexture(r, base, NULL, NULL);
    SDL_SetRenderTarget(r, glow_b);
    SDL_RenderTexture(r, glow_a, NULL, NULL);
    SDL_SetTextureScaleMode(base, old);
    return glow_b;
}

static void add_texture(SDL_Renderer *r, SDL_Texture *t, const SDL_FRect *d, float k)
{
    SDL_BlendMode old;
    Uint8 cr, cg, cb;
    SDL_GetTextureBlendMode(t, &old);
    SDL_GetTextureColorMod(t, &cr, &cg, &cb);
    Uint8 m = (Uint8)(SDL_clamp(k, 0.0f, 1.0f) * 255);
    SDL_SetTextureBlendMode(t, SDL_BLENDMODE_ADD);
    SDL_SetTextureColorMod(t, m, m, m);
    SDL_RenderTexture(r, t, NULL, d);
    SDL_SetTextureBlendMode(t, old);
    SDL_SetTextureColorMod(t, cr, cg, cb);
}

/* The finished image onto the window over a grid bent like a tube's face: the middle of each
   edge bulges out, the corners stay in (a cut of a sphere seen from the front). */
static void curved(SDL_Renderer *r, SDL_Texture *t, SDL_FRect d, float k)
{
    enum { NX = 32, NY = 24 };
    static SDL_Vertex v[(NX + 1) * (NY + 1)];
    static int idx[NX * NY * 6];
    SDL_FColor white = {1, 1, 1, 1};
    float cx = d.x + d.w / 2, cy = d.y + d.h / 2;
    /* the grid is scaled so that the middle of the edges touches the rectangle */
    for (int j = 0; j <= NY; j++)
        for (int i = 0; i <= NX; i++) {
            float u = (float)i / NX, w = (float)j / NY, x = u * 2 - 1, y = w * 2 - 1;
            float sx = (1 - k * y * y), sy = (1 - k * x * x);
            v[j * (NX + 1) + i] = (SDL_Vertex){{cx + x * sx * d.w / 2, cy + y * sy * d.h / 2}, white, {u, w}};
        }
    int n = 0;
    for (int j = 0; j < NY; j++)
        for (int i = 0; i < NX; i++) {
            int a = j * (NX + 1) + i, b = a + 1, c = a + NX + 1, e = c + 1;
            idx[n++] = a; idx[n++] = b; idx[n++] = e;
            idx[n++] = a; idx[n++] = e; idx[n++] = c;
        }
    SDL_RenderGeometry(r, t, v, (NX + 1) * (NY + 1), idx, n);
}

void crt_draw(SDL_Renderer *r, SDL_Texture *base, SDL_FRect d, int style, int curve)
{
    if (style <= CRT_OFF || style >= CRT_COUNT || !base) return;
    const Style *s = &STYLES[style];
    int w = (int)SDL_ceilf(d.w), h = (int)SDL_ceilf(d.h);
    float pitch = d.h / NG_HEIGHT;
    if (!lines || h != lines_h || style != lines_style || pitch != lines_pitch) {
        make_lines(r, h, pitch, s);
        lines_h = h;
        lines_style = style;
        lines_pitch = pitch;
    }
    if (!grille) grille = make_mask(r, 0, STYLES[CRT_ARCADE].mask);
    if (!dots) dots = make_mask(r, 1, STYLES[CRT_TV].mask);
    if (!vignette) make_vignette(r);
    if (!screen || w != screen_w || h != screen_h) {
        if (screen) SDL_DestroyTexture(screen);
        screen = target(r, w, h);
        if (screen) SDL_SetTextureBlendMode(screen, SDL_BLENDMODE_NONE);
        screen_w = w;
        screen_h = h;
    }
    SDL_Texture *out = SDL_GetRenderTarget(r);
    SDL_Texture *glow = s->glow > 0 ? blurred(r, base) : NULL;
    /* the whole effect at the output's pixels, in a texture of the image's size */
    SDL_SetRenderTarget(r, screen);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 255);
    SDL_RenderClear(r);
    SDL_FRect all = {0, 0, (float)w, (float)h};
    SDL_RenderTexture(r, base, NULL, &all);
    if (lines) SDL_RenderTexture(r, lines, NULL, &all);
    SDL_Texture *mask = s->mask <= 0 ? NULL : s->dots ? dots : grille;
    if (mask && pitch >= 2) SDL_RenderTextureTiled(r, mask, NULL, 1, &all);
    add_texture(r, base, &all, s->give_back);          /* part of the brightness back */
    if (glow) add_texture(r, glow, &all, s->glow);      /* bright areas bloom */
    if (vignette && s->vignette > 0) {
        SDL_SetTextureAlphaMod(vignette, (Uint8)(s->vignette * 255));
        SDL_RenderTexture(r, vignette, NULL, &all);
    }
    SDL_SetRenderTarget(r, out);
    if (!screen) return;
    if (curve && s->curve > 0) curved(r, screen, (SDL_FRect){d.x, d.y, (float)w, (float)h}, s->curve);
    else SDL_RenderTexture(r, screen, NULL, &(SDL_FRect){SDL_floorf(d.x), SDL_floorf(d.y), (float)w, (float)h});
}
