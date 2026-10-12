/* Fuente de los menús: tiles 0x300-0x37F de la capa fix del cartucho (fuente sombreada 8x8 en
 * orden ASCII, la de "CREDIT"). Índices: 1 contorno, 2-5 degradé de abajo hacia arriba, 7 sombra,
 * 15 brillo; 0 transparente. Los colores son propios del port (no los del juego), uno por tinta.
 * Se arma un atlas de 16 x 6 caracteres por tinta en una textura. Las minúsculas de esta fuente
 * son muy bajas y se confunden entre sí: el texto se dibuja en mayúsculas, como en el juego.
 */
#include "font.h"

#include <string.h>

#include "neogeo.h"

#define BASE 0x300
static SDL_Texture *atlas;

/* [tinta][índice] ARGB; los índices sin uso en la fuente toman el tono medio. */
static const uint32_t TINTAS[TINTA_N][6] = {
    /* contorno, degradé 2-5, brillo */
    {0xFF000000, 0xFF707AA4, 0xFF929DCA, 0xFFB6C0EC, 0xFFE8F0FF},     /* plata */
    {0xFF000000, 0xFFA04400, 0xFFD87A10, 0xFFF8B232, 0xFFFFE47A},     /* oro */
    {0xFF000000, 0xFF4A4A4A, 0xFF606060, 0xFF7A7A7A, 0xFF9A9A9A},     /* gris (desactivado) */
    {0xFF000000, 0xFF1E7A2A, 0xFF34A83E, 0xFF5CD064, 0xFFB2F7B0},     /* verde (sonando) */
};

static uint32_t color(int tinta, int v)
{
    const uint32_t *t = TINTAS[tinta];
    switch (v) {
    case 0: return 0;
    case 1: return t[0];
    case 2: case 3: case 4: case 5: return t[v - 1];
    case 7: return 0xFF181818;
    case 15: return 0xFFFFFFFF;
    default: return t[2];
    }
}

int fuente_crear(SDL_Renderer *r)
{
    if (!ng_roms.fix_juego) return -1;
    static uint32_t px[128 * 48 * TINTA_N];
    for (int tinta = 0; tinta < TINTA_N; tinta++)
        for (int c = 0; c < 96; c++) {
            const uint8_t *t = ng_roms.fix_juego + (size_t)(BASE + 0x20 + c) * 32;
            int ox = (c % 16) * 8, oy = tinta * 48 + (c / 16) * 8;
            for (int y = 0; y < 8; y++)
                for (int x = 0; x < 8; x++) {
                    uint8_t b = t[y * 4 + x / 2];
                    px[(oy + y) * 128 + ox + x] = color(tinta, (x & 1) ? b >> 4 : b & 0x0F);
                }
        }
    atlas = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STATIC, 128, 48 * TINTA_N);
    if (!atlas) return -1;
    SDL_UpdateTexture(atlas, NULL, px, 128 * 4);
    SDL_SetTextureBlendMode(atlas, SDL_BLENDMODE_BLEND);
    SDL_SetTextureScaleMode(atlas, SDL_SCALEMODE_PIXELART);   /* parejo con escalas no enteras */
    return 0;
}

void fuente_destruir(void)
{
    if (atlas) SDL_DestroyTexture(atlas);
    atlas = NULL;
}

float fuente_ancho(const char *s, float escala) { return (float)strlen(s) * 8 * escala; }

float fuente_escala_ajustada(const char *s, float maxima, float ancho)
{
    float e = ancho / fuente_ancho(s, 1);
    return e < maxima ? SDL_floorf(e * 2) / 2 : maxima;      /* en medios píxeles */
}

float fuente_factor(int w, int h)
{
    float k = SDL_min(w / 480.0f, h / 300.0f);
    return k < 0.5f ? 0.5f : k;
}

void fuente_texto(SDL_Renderer *r, float x, float y, float escala, int tinta, const char *s)
{
    if (!atlas) { SDL_RenderDebugText(r, x, y, s); return; }
    for (; *s; s++, x += 8 * escala) {
        int c = (unsigned char)*s;
        if (c >= 'a' && c <= 'z') c -= 'a' - 'A';
        if (c < 0x20 || c > 0x7F) c = '?';
        c -= 0x20;
        if (!c) continue;
        SDL_FRect o = {(float)(c % 16) * 8, (float)(tinta * 48 + (c / 16) * 8), 8, 8};
        SDL_FRect d = {x, y, 8 * escala, 8 * escala};
        SDL_RenderTexture(r, atlas, &o, &d);
    }
}

void fuente_centrado(SDL_Renderer *r, float cx, float y, float escala, int tinta, const char *s)
{
    fuente_texto(r, SDL_roundf(cx - fuente_ancho(s, escala) / 2), y, escala, tinta, s);
}
