/* Menú de música, efectos y voces, y vibración del mando (Fase 6B).
 *
 * Menú: al abrirlo se guarda el estado completo del juego (estado.c) y se deja de ejecutar el
 * 68000; el Z80 y el YM2610 avanzan solos (ng_sonido_solo_*). Cada sonido se reproduce como lo
 * hace el juego al cambiar de música: 03 03 06 (reinicio del driver), el comando de modo y el
 * comando, un cuadro entre cada uno. Al cerrar se restaura el estado: el juego sigue exactamente
 * donde estaba.
 *
 * Vibración: el juego no avisa de explosiones, pero sí envía un comando de sonido por cada una.
 * Se sigue el modo del driver y, para los efectos identificados (docs/FASE6_REMASTER.md), se pide
 * una vibración; dentro de un cuadro gana la más fuerte y no se corta una más fuerte en curso.
 */
#include "sonidos.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neogeo.h"

/* ------------------------------------------------------------------ menú */
static const char *const CATEGORIA[3] = {"Musica", "Efectos", "Voces"};
static const uint8_t MODO_CAT[3] = {MODO_MUSICA, MODO_EFECTOS, MODO_VOCES};

static struct {
    int abierto, categoria, sel[3], sonando;     /* sonando: índice en PERFIL_SONIDOS o -1 */
    uint8_t *estado;
    size_t tam;
    uint8_t cola[8];
    int n_cola, espera;
    Uint64 desde;                                /* inicio de la reproducción (ns) */
    uint8_t modo_vibracion;                      /* modo del driver que seguía la vibración */
} m;

static uint8_t *modo_driver(void);

static int indice(int cat, int pos)
{
    for (int i = 0, k = 0; i < PERFIL_N_SONIDOS; i++)
        if (PERFIL_SONIDOS[i].modo == MODO_CAT[cat] && k++ == pos) return i;
    return -1;
}

static int cuantos(int cat)
{
    int k = 0;
    for (int i = 0; i < PERFIL_N_SONIDOS; i++) k += PERFIL_SONIDOS[i].modo == MODO_CAT[cat];
    return k;
}

static void encolar(const uint8_t *c, int n)
{
    memcpy(m.cola, c, (size_t)n);
    m.n_cola = n;
    m.espera = 0;
}

static void reproducir(int i)
{
    const Sonido *s = &PERFIL_SONIDOS[i];
    uint8_t c[5] = {0x03, 0x03, 0x06, s->modo, s->comando};
    encolar(c, 5);
    m.sonando = i;
    m.desde = SDL_GetTicksNS();
}

static void detener(void)
{
    uint8_t c[3] = {0x03, 0x03, 0x06};
    encolar(c, 3);
    m.sonando = -1;
}

void sonidos_abrir(void)
{
    if (m.abierto) return;
    free(m.estado);
    m.estado = ng_estado_guardar(&m.tam);
    if (!m.estado) return;
    m.abierto = 1;
    m.sonando = -1;
    m.modo_vibracion = *modo_driver();
    detener();                                   /* silencia lo que sonaba en el juego */
}

void sonidos_cerrar(void)
{
    if (!m.abierto) return;
    m.abierto = 0;
    if (m.estado) ng_estado_cargar(m.estado, m.tam);
    *modo_driver() = m.modo_vibracion;
    free(m.estado);
    m.estado = NULL;
}

int sonidos_abierto(void) { return m.abierto; }

void sonidos_cuadro(void)
{
    if (m.n_cola && m.espera-- <= 0) {
        ng_sonido_solo_comando(m.cola[0]);
        memmove(m.cola, m.cola + 1, (size_t)--m.n_cola);
        m.espera = 0;                            /* el siguiente, en el próximo cuadro */
    }
    ng_sonido_solo_avanzar(NG_CICLOS_CUADRO);
}

void sonidos_navegar(int t)
{
    int n = cuantos(m.categoria);
    int *s = &m.sel[m.categoria];
    switch (t) {
    case 0: *s = (*s + n - 1) % n; break;
    case 1: *s = (*s + 1) % n; break;
    case 2: m.categoria = (m.categoria + 2) % 3; break;
    case 3: m.categoria = (m.categoria + 1) % 3; break;
    case 4: { int i = indice(m.categoria, *s); if (i >= 0) reproducir(i); break; }
    case 6: detener(); break;
    default: sonidos_cerrar(); break;
    }
}

static void texto(SDL_Renderer *r, float x, float y, const char *s, Uint8 cr, Uint8 cg, Uint8 cb)
{
    SDL_SetRenderDrawColor(r, cr, cg, cb, 255);
    SDL_RenderDebugText(r, x, y, s);
}

void sonidos_dibujar(SDL_Renderer *r, int w, int h)
{
    if (!m.abierto) return;
    int k = SDL_max(1, SDL_min(w / 360, h / 250));
    SDL_SetRenderScale(r, (float)k, (float)k);
    float vw = (float)w / k, vh = (float)h / k;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 200);
    SDL_RenderFillRect(r, NULL);
    const float L = 12, x0 = vw / 2 - 150;
    float y = 10;
    char b[128];
    texto(r, vw / 2 - 72, y, "MUSICA Y SONIDOS", 255, 210, 80);
    y += 2 * L;
    /* pestañas */
    float x = x0;
    for (int c = 0; c < 3; c++) {
        snprintf(b, sizeof b, c == m.categoria ? "[%s]" : " %s ", CATEGORIA[c]);
        if (c == m.categoria) texto(r, x, y, b, 255, 255, 255);
        else texto(r, x, y, b, 150, 150, 150);
        x += (float)(strlen(b) + 2) * 8;
    }
    y += 2 * L;
    /* lista con desplazamiento */
    int n = cuantos(m.categoria), sel = m.sel[m.categoria];
    int visibles = (int)((vh - y - 4 * L) / L);
    if (visibles < 3) visibles = 3;
    int primero = sel - visibles / 2;
    if (primero > n - visibles) primero = n - visibles;
    if (primero < 0) primero = 0;
    for (int p = primero; p < n && p < primero + visibles; p++) {
        int i = indice(m.categoria, p);
        const Sonido *s = &PERFIL_SONIDOS[i];
        char dur[16];
        if (s->decimas == 0xFFFF) snprintf(dur, sizeof dur, "bucle");
        else snprintf(dur, sizeof dur, "%d.%d s", s->decimas / 10, s->decimas % 10);
        snprintf(b, sizeof b, "%-28s %6s", s->nombre, dur);
        int es = p == sel, suena = i == m.sonando;
        texto(r, x0, y, es ? ">" : " ", 255, 210, 80);
        texto(r, x0 + 12, y, b, suena ? 120 : es ? 255 : 190, suena ? 230 : es ? 255 : 190, suena ? 120 : es ? 255 : 190);
        y += L;
    }
    /* estado y ayuda */
    y = vh - 5 * L;
    if (m.sonando >= 0) {
        double seg = (double)(SDL_GetTicksNS() - m.desde) / 1e9;
        snprintf(b, sizeof b, "Sonando: %s (%02d:%02d)", PERFIL_SONIDOS[m.sonando].nombre, (int)seg / 60, (int)seg % 60);
        texto(r, x0, y, b, 120, 230, 120);
    }
    texto(r, x0, y + L + 4, "Enter/A: escuchar   Supr/Y: parar", 170, 170, 170);
    texto(r, x0, y + 2 * L + 4, "</> o LB/RB: categoria   Esc/B: volver", 170, 170, 170);
    SDL_SetRenderScale(r, 1, 1);
}

/* ------------------------------------------------------------------ vibración */
typedef struct { uint8_t comando; uint16_t baja, alta; uint16_t ms; } Vibracion;

/* Efectos (modo 10) identificados en la Misión 1. Baja = motor grande (golpes), alta = motor
   chico (zumbido). */
static const Vibracion VIBRACIONES[] = {
    {0x22, 0xFFFF, 0xA000, 380},                 /* explosión grande */
    {0xF2, 0xE000, 0x9000, 300},                 /* impacto en blindaje */
    {0x25, 0xC000, 0x8000, 260},                 /* explosión */
    {0x21, 0xC000, 0x8000, 260},                 /* explosión */
    {0xA3, 0xB000, 0x7000, 240},                 /* explosión */
    {0xF5, 0xA000, 0x6000, 200},                 /* explosión de granada o mortero */
    {0x84, 0x9000, 0x7000, 140},                 /* cañón del Slug */
    {0x8C, 0x2800, 0x5000, 45},                  /* vulcan del Slug */
    {0x8D, 0x2000, 0x4000, 40},                  /* ráfaga */
    {0xF7, 0x1800, 0x3000, 35},                  /* pistola */
};

static struct {
    SDL_Gamepad **mandos;
    int n_mandos, intensidad;
    uint8_t modo;
    int pedida;                                  /* índice en VIBRACIONES o -1 */
    Uint64 fin;                                  /* fin de la vibración en curso (ns) */
    uint32_t fuerza;                             /* de la vibración en curso */
} v = {NULL, 0, 70, 0xFF, -1, 0, 0};

static void al_comando(uint8_t c)
{
    if (c == MODO_MUSICA || c == MODO_EFECTOS || c == MODO_VOCES) { v.modo = c; return; }
    if (c == 0x03) { v.modo = 0xFF; return; }
    if (v.modo != MODO_EFECTOS) return;
    for (int i = 0; i < (int)(sizeof VIBRACIONES / sizeof VIBRACIONES[0]); i++)
        if (VIBRACIONES[i].comando == c) {
            if (v.pedida < 0 || VIBRACIONES[i].baja > VIBRACIONES[v.pedida].baja) v.pedida = i;
            return;
        }
}

void vibracion_iniciar(SDL_Gamepad **mandos, int n)
{
    v.mandos = mandos;
    v.n_mandos = n;
    ng_al_comando_sonido = al_comando;
}

void vibracion_intensidad(int intensidad) { v.intensidad = intensidad; }

static uint8_t *modo_driver(void) { return &v.modo; }

void vibracion_cuadro(void)
{
    if (v.pedida < 0) return;
    const Vibracion *p = &VIBRACIONES[v.pedida];
    v.pedida = -1;
    if (v.intensidad <= 0 || m.abierto) return;
    Uint64 ahora = SDL_GetTicksNS();
    if (ahora < v.fin && p->baja < v.fuerza) return;       /* no cortar una más fuerte */
    Uint16 baja = (Uint16)(p->baja * (uint32_t)v.intensidad / 100);
    Uint16 alta = (Uint16)(p->alta * (uint32_t)v.intensidad / 100);
    for (int j = 0; j < v.n_mandos; j++)
        if (v.mandos[j]) SDL_RumbleGamepad(v.mandos[j], baja, alta, p->ms);
    v.fin = ahora + (Uint64)p->ms * 1000000ULL;
    v.fuerza = p->baja;
}
