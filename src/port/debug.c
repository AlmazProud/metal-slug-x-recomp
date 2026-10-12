/* Modo debug (Fase 6A).
 *
 * Panel (F3):
 *   - FPS del anfitrión, cuadro emulado y turbo;
 *   - lógica del juego en Hz: 60 / (frames que duró el último ciclo, byte del perfil), 30 Hz es
 *     lo normal y 20 o 15 Hz es la ralentización;
 *   - carga del 68000 en el cuadro: inicios de línea en que NO estaba en el bucle de espera;
 *   - gráfico de los últimos 4 s (barra = carga; rojo = cuadro de un ciclo lento);
 *   - frames perdidos por la ralentización en los últimos 10 s.
 * Teclas: F6 avanza un cuadro (en pausa), Ctrl+1 / Ctrl+2 ocultan sprites / layer fix, Ctrl+3
 * cajas de sprites, Ctrl+4 palette, Ctrl+5 memoria (RePág / AvPág / Inicio para moverse).
 */
#include "debug.h"

#include <stdio.h>
#include <string.h>

#include "neogeo.h"
#include "profile.h"

#define HISTORIA 600                         /* 10 s */
#define GRAFICO 240                          /* 4 s */

static struct {
    int active, cajas, palette, memoria;
    uint32_t mem_dir;                        /* offset en la RAM de trabajo */
    uint8_t carga[HISTORIA], logica[HISTORIA];
    unsigned n;                              /* frames registrados */
    Uint64 fps_desde;
    int fps_cuadros;
    double fps;
    SDL_Texture *tex_paleta;
} d;

static void vaciar_cajas(void)
{
    for (int i = 0; i < 381; i++) ng_dbg.box[i] = ng_dbg.box_current[i] = (NgBox){1, 0, 1, 0};
}

void debug_enable(int active)
{
    d.active = active;
    ng_dbg.active = active;
    ng_dbg.wait_start = PROFILE_WAIT_START;
    ng_dbg.wait_end = PROFILE_WAIT_END;
    if (!active) ng_dbg.hide = 0;
    vaciar_cajas();
    d.n = 0;
    const char *vistas = getenv("PD_DEBUG_VISTAS");    /* tests: "cajas,palette,memoria" */
    if (active && vistas) {
        d.cajas = strstr(vistas, "cajas") != NULL;
        d.palette = strstr(vistas, "palette") != NULL;
        d.memoria = strstr(vistas, "memoria") != NULL;
    }
}

int debug_enabled(void) { return d.active; }

int debug_key(const SDL_KeyboardEvent *e)
{
    if (e->scancode == SDL_SCANCODE_F3) { debug_enable(!d.active); return DEBUG_HANDLED; }
    if (!d.active) return DEBUG_NONE;
    if (e->scancode == SDL_SCANCODE_F6) return DEBUG_STEP;
    if (e->mod & SDL_KMOD_CTRL) {
        switch (e->scancode) {
        case SDL_SCANCODE_1: ng_dbg.hide ^= NG_HIDE_SPRITES; return DEBUG_HANDLED;
        case SDL_SCANCODE_2: ng_dbg.hide ^= NG_HIDE_FIX; return DEBUG_HANDLED;
        case SDL_SCANCODE_3: d.cajas = !d.cajas; return DEBUG_HANDLED;
        case SDL_SCANCODE_4: d.palette = !d.palette; return DEBUG_HANDLED;
        case SDL_SCANCODE_5: d.memoria = !d.memoria; return DEBUG_HANDLED;
        default: break;
        }
    }
    if (d.memoria) {
        if (e->scancode == SDL_SCANCODE_PAGEUP) { d.mem_dir = (d.mem_dir - 0x100) & 0xFF00; return DEBUG_HANDLED; }
        if (e->scancode == SDL_SCANCODE_PAGEDOWN) { d.mem_dir = (d.mem_dir + 0x100) & 0xFF00; return DEBUG_HANDLED; }
        if (e->scancode == SDL_SCANCODE_HOME) { d.mem_dir = 0; return DEBUG_HANDLED; }
    }
    return DEBUG_NONE;
}

void debug_frame(void)
{
    if (!d.active) return;
    int espera = ng_dbg.wait_lines;
    int carga = 100 - espera * 100 / NG_LINES;
    unsigned i = d.n++ % HISTORIA;
    d.carga[i] = (uint8_t)(carga < 0 ? 0 : carga);
    d.logica[i] = ng.ram[PROFILE_RAM_FRAMES_PER_TICK];
}

/* ------------------------------------------------------------------ dibujo */
static void text(SDL_Renderer *r, float x, float y, const char *s, Uint8 cr, Uint8 cg, Uint8 cb)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderDebugText(r, x, y, s);
}

static void fondo(SDL_Renderer *r, float x, float y, float w, float h)
{
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 180);
    SDL_FRect f = {x, y, w, h};
    SDL_RenderFillRect(r, &f);
}

static void dibujar_cajas(SDL_Renderer *r, SDL_FRect j)
{
    float sx = j.w / NG_WIDTH, sy = j.h / NG_HEIGHT;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    for (int i = 0; i < 381; i++) {
        const NgBox *c = &ng_dbg.box[i];
        if (c->x1 < c->x0) continue;
        if (c->y1 - c->y0 >= 200) continue;             /* columnas del fondo: ocupan todo el alto */
        SDL_FRect f = {j.x + c->x0 * sx, j.y + c->y0 * sy, (c->x1 - c->x0 + 1) * sx, (c->y1 - c->y0 + 1) * sy};
        /* color estable por sprite */
        SDL_SetRenderDrawColor(r, (Uint8)(80 + (i * 97) % 176), (Uint8)(80 + (i * 57) % 176), (Uint8)(80 + (i * 31) % 176), 200);
        SDL_RenderRect(r, &f);
    }
}

static void dibujar_paleta(SDL_Renderer *r, float x, float y, float lado)
{
    if (!d.tex_paleta) {
        d.tex_paleta = SDL_CreateTexture(r, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, 64, 64);
        SDL_SetTextureScaleMode(d.tex_paleta, SDL_SCALEMODE_NEAREST);
    }
    /* 4096 colores del bank active: cada fila son 4 paletas de 16 colores. */
    const uint32_t *pens = ng.pen[ng.shadow] + ((uint32_t)ng.palette_bank << 12);
    SDL_UpdateTexture(d.tex_paleta, NULL, pens, 64 * 4);
    fondo(r, x - 4, y - 14, lado + 8, lado + 18);
    text(r, x, y - 11, "Paleta (256 x 16)", 255, 210, 80);
    SDL_FRect f = {x, y, lado, lado};
    SDL_RenderTexture(r, d.tex_paleta, NULL, &f);
}

static void dibujar_memoria(SDL_Renderer *r, float x, float y)
{
    char b[96];
    const float L = 10;
    fondo(r, x - 4, y - 4, 58 * 8 + 8, 18 * L + 6);
    snprintf(b, sizeof b, "RAM %06X  (RePag/AvPag/Inicio)", 0x100000 + d.mem_dir);
    text(r, x, y, b, 255, 210, 80);
    for (int f = 0; f < 16; f++) {
        uint32_t a = d.mem_dir + (uint32_t)f * 16;
        int n = snprintf(b, sizeof b, "%06X ", 0x100000 + a);
        for (int k = 0; k < 16; k++)
            n += snprintf(b + n, sizeof b - (size_t)n, k == 8 ? "  %02X" : " %02X", ng.ram[(a + (uint32_t)k) & 0xFFFF]);
        text(r, x, y + (f + 2) * L, b, 220, 220, 220);
    }
}

void debug_draw(SDL_Renderer *r, SDL_FRect juego, int w, int h, int turbo)
{
    Uint64 ahora = SDL_GetTicksNS();
    d.fps_cuadros++;
    if (ahora - d.fps_desde >= 1000000000ULL) {
        d.fps = d.fps_cuadros * 1e9 / (double)(ahora - d.fps_desde);
        d.fps_desde = ahora;
        d.fps_cuadros = 0;
    }
    if (!d.active) return;
    if (d.cajas) dibujar_cajas(r, juego);

    int k = SDL_max(1, SDL_min(w / 640, h / 480));
    SDL_SetRenderScale(r, (float)k, (float)k);
    float vw = (float)w / k, vh = (float)h / k;
    const float L = 10;
    char b[128];
    float x = 8, y = 8;
    fondo(r, 4, 4, 51 * 8, 10 * L + 48);
    text(r, x, y, "DEBUG  F3 cerrar  F6 avanzar  P pausa", 255, 210, 80);

    snprintf(b, sizeof b, "FPS %5.1f  cuadro %llu  turbo x%d", d.fps, (unsigned long long)ng.frames, 1 << turbo);
    text(r, x, y + L, b, 220, 220, 220);

    unsigned ult = (d.n + HISTORIA - 1) % HISTORIA;
    int l = d.n ? d.logica[ult] : 0;
    if (l >= 1 && l <= 8) snprintf(b, sizeof b, "Logica %4.1f Hz (%d frames por ciclo)", 60.0 / l, l);
    else snprintf(b, sizeof b, "Logica  -   (fuera del juego)");
    if (l > 2 && l <= 8) text(r, x, y + 2 * L, b, 255, 90, 90);
    else text(r, x, y + 2 * L, b, 120, 230, 120);

    /* frames perdidos: un cuadro de un ciclo de l frames pierde (l - 2) / l */
    double perdidos = 0;
    unsigned cuantos = d.n < HISTORIA ? d.n : HISTORIA;
    for (unsigned i = 0; i < cuantos; i++)
        if (d.logica[i] > 2 && d.logica[i] <= 8) perdidos += (d.logica[i] - 2) / (double)d.logica[i];
    /* La lógica corre cada 2 frames: la carga de un cuadro suelto alterna; se muestra la media y
       el pico del último medio second. */
    int suma = 0, pico = 0, m = d.n < 30 ? (int)d.n : 30;
    for (int i = 0; i < m; i++) {
        int c = d.carga[(d.n - 1 - (unsigned)i) % HISTORIA];
        suma += c;
        if (c > pico) pico = c;
    }
    snprintf(b, sizeof b, "CPU 68000 media %3d%% pico %3d%%", m ? suma / m : 0, pico);
    text(r, x, y + 3 * L, b, 220, 220, 220);
    snprintf(b, sizeof b, "Cuadros perdidos (10 s): %4.1f%%", cuantos ? 100.0 * perdidos / cuantos : 0.0);
    text(r, x, y + 4 * L, b, 220, 220, 220);

    /* gráfico: carga por cuadro */
    float gx = x, gy = y + 5 * L + 2, gh = 40;
    SDL_SetRenderDrawColor(r, 60, 60, 60, 255);
    SDL_FRect marco = {gx - 1, gy - 1, GRAFICO * 1.5f + 2, gh + 2};
    SDL_RenderRect(r, &marco);
    unsigned desde = d.n > GRAFICO ? d.n - GRAFICO : 0;
    for (unsigned c = desde; c < d.n; c++) {
        unsigned i = c % HISTORIA;
        float alto = d.carga[i] * gh / 100;
        int lento = d.logica[i] > 2 && d.logica[i] <= 8;
        SDL_SetRenderDrawColor(r, lento ? 230 : 80, lento ? 70 : 200, lento ? 70 : 90, 255);
        SDL_FRect barra = {gx + (c - desde) * 1.5f, gy + gh - alto, 1.5f, alto};
        SDL_RenderFillRect(r, &barra);
    }
    float ty = gy + gh + 4;
    snprintf(b, sizeof b, "Ctrl+1 sprites %s  Ctrl+2 fix %s", (ng_dbg.hide & NG_HIDE_SPRITES) ? "NO" : "si",
             (ng_dbg.hide & NG_HIDE_FIX) ? "NO" : "si");
    text(r, x, ty, b, 170, 170, 170);
    snprintf(b, sizeof b, "Ctrl+3 cajas %s  Ctrl+4 palette %s  Ctrl+5 RAM %s", d.cajas ? "si" : "no",
             d.palette ? "si" : "no", d.memoria ? "si" : "no");
    text(r, x, ty + L, b, 170, 170, 170);

    if (d.palette) dibujar_paleta(r, vw - 64 * 3 - 8, 22, 64 * 3);
    if (d.memoria) dibujar_memoria(r, 8, vh - 18 * L - 8);
    SDL_SetRenderScale(r, 1, 1);
}
