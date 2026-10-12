/* Logo de la pantalla de título, tomado del propio juego (de la copia del usuario).
 *
 * En la pantalla de título del attract (cuadro ~1150 desde el encendido con la SRAM vacía), el logo
 * son los sprites 41-124 (sombra, "METAL SLUG", la X y "SUPER VEHICLE-001") más el 380 ("TM");
 * el fondo rojo son los sprites 21-40 (src/port/perfil.h). Se redibujan solo esos sprites con
 * ng_video_capturar_sprites, se recorta el rectángulo con contenido y se guarda como PNG en la
 * carpeta del usuario: solo se genera la primera vez. El logo no entra al repositorio.
 */
#include "logo.h"

#include <stdlib.h>
#include <string.h>

#include "neogeo.h"
#include "perfil.h"

static SDL_Texture *tex;

SDL_Texture *logo_textura(void) { return tex; }

void logo_destruir(void)
{
    if (tex) SDL_DestroyTexture(tex);
    tex = NULL;
}

static uint32_t tile_de(int s)
{
    return ((uint32_t)(ng.vram[(s << 6) + 1] << 12) & 0xF0000) | ng.vram[s << 6];
}

/* Corre la máquina recién encendida hasta el título y captura el logo; NULL si no aparece. */
static SDL_Surface *generar(void)
{
    static uint32_t a[NG_ALTO][NG_ANCHO], b[NG_ALTO][NG_ANCHO];
    int esperar = -1;
    ng_encender();
    for (int f = 0; f < PERFIL_LOGO_CUADROS_MAX; f++) {
        ng_ejecutar_cuadro();
        if (esperar < 0 && tile_de(PERFIL_LOGO_TM) == PERFIL_LOGO_TM_TILE) esperar = 30;   /* que termine de entrar */
        if (esperar >= 0 && esperar-- == 0) break;
    }
    if (esperar != -1) return NULL;
    ng_video_capturar_sprites(&a[0][0], PERFIL_LOGO_DESDE, PERFIL_LOGO_HASTA);
    ng_video_capturar_sprites(&b[0][0], PERFIL_LOGO_TM, PERFIL_LOGO_TM);
    int x0 = NG_ANCHO, y0 = NG_ALTO, x1 = -1, y1 = -1;
    for (int y = 0; y < NG_ALTO; y++)
        for (int x = 0; x < NG_ANCHO; x++) {
            if (b[y][x]) a[y][x] = b[y][x];
            if (!a[y][x]) continue;
            a[y][x] |= 0xFF000000u;
            if (x < x0) x0 = x;
            if (x > x1) x1 = x;
            if (y < y0) y0 = y;
            if (y > y1) y1 = y;
        }
    if (x1 < 0) return NULL;
    SDL_Surface *s = SDL_CreateSurface(x1 - x0 + 1, y1 - y0 + 1, SDL_PIXELFORMAT_ARGB8888);
    if (!s) return NULL;
    for (int y = y0; y <= y1; y++)
        memcpy((uint8_t *)s->pixels + (size_t)(y - y0) * s->pitch, &a[y][x0], (size_t)(x1 - x0 + 1) * 4);
    return s;
}

int logo_preparar(SDL_Renderer *r, const char *cache)
{
    SDL_Surface *s = SDL_LoadPNG(cache);
    if (!s) {
        /* La máquina vuelve exactamente a como estaba (estado completo, SRAM incluida). */
        size_t tam;
        uint8_t *estado = ng_estado_guardar(&tam);
        int audio = ng_audio_activo;
        void (*gancho)(uint8_t) = ng_al_comando_sonido;
        ng_audio_activo = 0;
        ng_al_comando_sonido = NULL;
        s = generar();
        ng_audio_activo = audio;
        ng_al_comando_sonido = gancho;
        if (estado) { ng_estado_cargar(estado, tam); free(estado); }
        if (s) SDL_SavePNG(s, cache);
    }
    if (!s) return -1;
    tex = SDL_CreateTextureFromSurface(r, s);
    SDL_DestroySurface(s);
    if (!tex) return -1;
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_PIXELART);
    return 0;
}
