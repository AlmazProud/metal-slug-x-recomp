/* Menú de música, efectos y voces, y vibración del mando (Fases 6B y 6C).
 *
 * Menú: al abrirlo se guarda el estado completo del juego (savestate.c) y se deja de ejecutar el
 * 68000; el Z80 y el YM2610 avanzan solos (ng_sonido_solo_*). Cada sonido se reproduce como lo
 * hace el juego al cambiar de música: 03 03 06 (reinicio del driver), el comando de modo y el
 * comando, un cuadro entre cada uno. Al cerrar se restaura el estado: el juego sigue exactamente
 * donde estaba.
 *
 * Pestañas: Música, Efectos, Voces y Recientes (los últimos sonidos que pidió el juego, para
 * identificar uno: por ejemplo, disparar el láser y abrir el menú). El usuario puede renombrar
 * cada sonido (F2 o R) y elegir su vibración (V, o X en el mando): se guarda en sonidos.ini.
 *
 * Vibración: el juego no avisa de explosiones, pero sí envía un comando de sonido por cada una.
 * Se sigue el modo del driver y se pide la vibración del sonido: la del usuario o, si no eligió,
 * la de la tabla de efectos identificados (docs/PHASE6_REMASTER.md). Dentro de un cuadro gana la
 * más fuerte y no se corta una más fuerte en curso.
 */
#include "sounds.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "font.h"
#include "neogeo.h"

/* ------------------------------------------------------------------ datos del usuario */
enum { VIB_AUTO = -1, VIB_NO, VIB_LEVE, VIB_MEDIA, VIB_FUERTE, VIB_N };
static const char *const VIB_NOMBRE[VIB_N] = {"no", "leve", "media", "fuerte"};

static int modo_indice(uint8_t modo)
{
    return modo == MODO_MUSICA ? 0 : modo == MODO_EFECTOS ? 1 : modo == MODO_VOCES ? 2 : -1;
}

static struct {
    char ruta[1100];
    char *nombre[3][256];                /* nombres del usuario (NULL = el del perfil) */
    int8_t vib[3][256];                  /* VIB_* del usuario (VIB_AUTO = la de la tabla) */
} u;

/* Copias propias: SDL_strdup reserva con el asignador de SDL3.dll y liberarlas con free() de otro
   runtime de C rompía el montón (cuelgue al renombrar dos veces el mismo sonido). */
static char *copiar(const char *s)
{
    size_t n = strlen(s) + 1;
    char *c = malloc(n);
    if (c) memcpy(c, s, n);
    return c;
}

static void usuario_guardar(void)
{
    FILE *f = fopen(u.ruta, "w");
    if (!f) return;
    static const uint8_t MODO[3] = {MODO_MUSICA, MODO_EFECTOS, MODO_VOCES};
    fprintf(f, "; Nombres y vibración de los sonidos de Metal Slug X (los escribe el menú de sonido).\n"
               "; Clave: modo (00 música, 10 efectos, 11 voces) y comando, en hexadecimal.\n\n[nombres]\n");
    for (int m = 0; m < 3; m++)
        for (int c = 0; c < 256; c++)
            if (u.nombre[m][c]) fprintf(f, "%02X%02X=%s\n", MODO[m], c, u.nombre[m][c]);
    fprintf(f, "\n; vibración: no, leve, media o fuerte\n[vibracion]\n");
    for (int m = 0; m < 3; m++)
        for (int c = 0; c < 256; c++)
            if (u.vib[m][c] != VIB_AUTO) fprintf(f, "%02X%02X=%s\n", MODO[m], c, VIB_NOMBRE[u.vib[m][c]]);
    fclose(f);
}

static void usuario_cargar(void)
{
    memset(u.vib, VIB_AUTO, sizeof u.vib);
    FILE *f = fopen(u.ruta, "r");
    if (!f) return;
    char linea[256], seccion[32] = "";
    while (fgets(linea, sizeof linea, f)) {
        linea[strcspn(linea, "\r\n")] = 0;
        if (linea[0] == '[') { snprintf(seccion, sizeof seccion, "%s", linea); continue; }
        unsigned clave;
        int usados = 0;
        if (sscanf(linea, "%4x=%n", &clave, &usados) != 1 || !usados) continue;
        int m = modo_indice((uint8_t)(clave >> 8)), c = clave & 0xFF;
        const char *v = linea + usados;
        if (m < 0) continue;
        if (!strcmp(seccion, "[nombres]") && *v) {
            free(u.nombre[m][c]);
            u.nombre[m][c] = copiar(v);
        } else if (!strcmp(seccion, "[vibracion]")) {
            for (int i = 0; i < VIB_N; i++) if (!strcmp(v, VIB_NOMBRE[i])) u.vib[m][c] = (int8_t)i;
        }
    }
    fclose(f);
}

static const Sonido *perfil(uint8_t modo, uint8_t cmd)
{
    for (int i = 0; i < PERFIL_N_SONIDOS; i++)
        if (PERFIL_SONIDOS[i].modo == modo && PERFIL_SONIDOS[i].comando == cmd) return &PERFIL_SONIDOS[i];
    return NULL;
}

static const char *nombre(uint8_t modo, uint8_t cmd)
{
    static char b[40];
    int m = modo_indice(modo);
    if (m >= 0 && u.nombre[m][cmd]) return u.nombre[m][cmd];
    const Sonido *s = perfil(modo, cmd);
    if (s) return s->nombre;
    snprintf(b, sizeof b, "%s %02X", modo == MODO_MUSICA ? "Pista" : modo == MODO_VOCES ? "Voz" : "Efecto", cmd);
    return b;
}

/* ------------------------------------------------------------------ vibración: tabla */
typedef struct { uint16_t baja, alta, ms; } Vibracion;
static const Vibracion NIVEL[VIB_N] = {{0, 0, 0}, {0x2000, 0x4000, 45}, {0x9000, 0x7000, 150}, {0xE000, 0x9800, 320}};

/* Efectos (modo 10) identificados en la Misión 1. Baja = motor grande (golpes), alta = motor
   chico (zumbido). */
static const struct { uint8_t comando; Vibracion v; } TABLA[] = {
    {0x22, {0xFFFF, 0xA000, 380}},               /* explosión grande */
    {0xF2, {0xE000, 0x9000, 300}},               /* impacto en blindaje */
    {0x25, {0xC000, 0x8000, 260}},               /* explosión */
    {0x21, {0xC000, 0x8000, 260}},               /* explosión */
    {0xA3, {0xB000, 0x7000, 240}},               /* explosión */
    {0xF5, {0xA000, 0x6000, 200}},               /* explosión de granada o mortero */
    {0x84, {0x9000, 0x7000, 140}},               /* cañón del Slug */
    {0x8C, {0x2800, 0x5000, 45}},                /* vulcan del Slug */
    {0x8D, {0x2000, 0x4000, 40}},                /* ráfaga */
    {0xF7, {0x1800, 0x3000, 35}},                /* pistola */
};

static const Vibracion *vibracion_de(uint8_t modo, uint8_t cmd)
{
    int m = modo_indice(modo);
    if (m < 0 || modo == MODO_MUSICA) return NULL;
    if (u.vib[m][cmd] != VIB_AUTO) return u.vib[m][cmd] ? &NIVEL[u.vib[m][cmd]] : NULL;
    if (modo != MODO_EFECTOS) return NULL;
    for (size_t i = 0; i < sizeof TABLA / sizeof TABLA[0]; i++)
        if (TABLA[i].comando == cmd) return &TABLA[i].v;
    return NULL;
}

/* ------------------------------------------------------------------ menú */
enum { CAT_MUSICA, CAT_EFECTOS, CAT_VOCES, CAT_RECIENTES, CAT_N };
static const char *const CATEGORIA[CAT_N] = {"Musica", "Efectos", "Voces", "Recientes"};
static const uint8_t MODO_CAT[3] = {MODO_MUSICA, MODO_EFECTOS, MODO_VOCES};
#define RECIENTES 16

typedef struct { uint8_t modo, cmd; } Clave;

static struct {
    SDL_Window *ventana;
    int abierto, categoria, sel[CAT_N];
    int sonando;                                 /* 1 si hay algo reproduciéndose */
    Clave actual;
    uint8_t *estado;
    size_t tam;
    uint8_t cola[8];
    int n_cola;
    Uint64 desde;                                /* inicio de la reproducción (ns) */
    uint8_t modo_vibracion;                      /* modo del driver que seguía la vibración */
    Clave reciente[RECIENTES];                   /* el más nuevo primero */
    int n_reciente;
    int editando;
    char texto[32];
    Uint64 ignorar_hasta;                        /* el texto de la tecla que abrió la edición */
} m;

static uint8_t *modo_driver(void);

static void anotar_reciente(uint8_t modo, uint8_t cmd)
{
    int i;
    for (i = 0; i < m.n_reciente; i++)
        if (m.reciente[i].modo == modo && m.reciente[i].cmd == cmd) break;
    if (i == m.n_reciente && m.n_reciente < RECIENTES) m.n_reciente++;
    if (i == RECIENTES) i = RECIENTES - 1;
    memmove(m.reciente + 1, m.reciente, (size_t)i * sizeof m.reciente[0]);
    m.reciente[0] = (Clave){modo, cmd};
}

/* Lista de la pestaña: hasta `max` sonidos. */
static int lista(int cat, Clave *out, int max)
{
    if (cat == CAT_RECIENTES) {
        memcpy(out, m.reciente, (size_t)m.n_reciente * sizeof *out);
        return m.n_reciente;
    }
    int n = 0;
    for (int i = 0; i < PERFIL_N_SONIDOS && n < max; i++)
        if (PERFIL_SONIDOS[i].modo == MODO_CAT[cat]) out[n++] = (Clave){PERFIL_SONIDOS[i].modo, PERFIL_SONIDOS[i].comando};
    return n;
}

static int seleccionado(Clave *c)
{
    Clave l[256];
    int n = lista(m.categoria, l, 256);
    if (!n) return 0;
    if (m.sel[m.categoria] >= n) m.sel[m.categoria] = n - 1;
    *c = l[m.sel[m.categoria]];
    return 1;
}

static void encolar(const uint8_t *c, int n)
{
    memcpy(m.cola, c, (size_t)n);
    m.n_cola = n;
}

static void reproducir(Clave c)
{
    uint8_t s[5] = {0x03, 0x03, 0x06, c.modo, c.cmd};
    encolar(s, 5);
    m.sonando = 1;
    m.actual = c;
    m.desde = SDL_GetTicksNS();
}

static void detener(void)
{
    uint8_t c[3] = {0x03, 0x03, 0x06};
    encolar(c, 3);
    m.sonando = 0;
}

void sonidos_iniciar(SDL_Window *ventana, const char *ruta)
{
    m.ventana = ventana;
    snprintf(u.ruta, sizeof u.ruta, "%s", ruta);
    usuario_cargar();
}

void sonidos_abrir(void)
{
    if (m.abierto) return;
    free(m.estado);
    m.estado = ng_estado_guardar(&m.tam);
    if (!m.estado) return;
    m.abierto = 1;
    m.sonando = 0;
    m.modo_vibracion = *modo_driver();
    detener();                                   /* silencia lo que sonaba en el juego */
}

void sonidos_cerrar(void)
{
    if (!m.abierto) return;
    if (m.editando) { m.editando = 0; if (m.ventana) SDL_StopTextInput(m.ventana); }
    m.abierto = 0;
    if (m.estado) ng_estado_cargar(m.estado, m.tam);
    *modo_driver() = m.modo_vibracion;
    free(m.estado);
    m.estado = NULL;
}

int sonidos_abierto(void) { return m.abierto; }

void sonidos_cuadro(void)
{
    if (m.n_cola) {                              /* un comando por cuadro */
        ng_sonido_solo_comando(m.cola[0]);
        memmove(m.cola, m.cola + 1, (size_t)--m.n_cola);
    }
    ng_sonido_solo_avanzar(NG_CICLOS_CUADRO);
}

static void renombrar(void)
{
    Clave c;
    if (!seleccionado(&c) || !m.ventana) return;
    snprintf(m.texto, sizeof m.texto, "%s", nombre(c.modo, c.cmd));
    m.editando = 1;
    m.ignorar_hasta = SDL_GetTicksNS() + 50000000ULL;
    SDL_StartTextInput(m.ventana);
}

static void cambiar_vibracion(void)
{
    Clave c;
    int i;
    if (!seleccionado(&c) || (i = modo_indice(c.modo)) < 0 || c.modo == MODO_MUSICA) return;
    int v = u.vib[i][c.cmd] + 1;                 /* auto -> no -> leve -> media -> fuerte -> auto */
    u.vib[i][c.cmd] = (int8_t)(v >= VIB_N ? VIB_AUTO : v);
    usuario_guardar();
}

void sonidos_navegar(int t)
{
    Clave l[256], c;
    int n = lista(m.categoria, l, 256);
    int *s = &m.sel[m.categoria];
    switch (t) {
    case 0: if (n) *s = (*s + n - 1) % n; break;
    case 1: if (n) *s = (*s + 1) % n; break;
    case 2: m.categoria = (m.categoria + CAT_N - 1) % CAT_N; break;
    case 3: m.categoria = (m.categoria + 1) % CAT_N; break;
    case 4: if (seleccionado(&c)) reproducir(c); break;
    case 6: detener(); break;
    case 7: renombrar(); break;
    case 8: cambiar_vibracion(); break;
    default: sonidos_cerrar(); break;
    }
}

int sonidos_editando(void) { return m.editando; }

void sonidos_texto(const char *s)
{
    if (!m.editando || SDL_GetTicksNS() < m.ignorar_hasta) return;
    size_t n = strlen(m.texto);
    for (; *s && n + 1 < sizeof m.texto; s++)
        if ((unsigned char)*s >= 0x20 && (unsigned char)*s < 0x7F) m.texto[n++] = *s;   /* la fuente es ASCII */
    m.texto[n] = 0;
}

void sonidos_tecla_edicion(SDL_Scancode sc)
{
    if (sc == SDL_SCANCODE_BACKSPACE) {
        size_t n = strlen(m.texto);
        if (n) m.texto[n - 1] = 0;
        return;
    }
    if (sc != SDL_SCANCODE_RETURN && sc != SDL_SCANCODE_KP_ENTER && sc != SDL_SCANCODE_ESCAPE) return;
    m.editando = 0;
    SDL_StopTextInput(m.ventana);
    Clave c;
    int i;
    if (sc == SDL_SCANCODE_ESCAPE || !seleccionado(&c) || (i = modo_indice(c.modo)) < 0) return;
    free(u.nombre[i][c.cmd]);
    u.nombre[i][c.cmd] = NULL;
    const Sonido *p = perfil(c.modo, c.cmd);
    if (m.texto[0] && !(p && !strcmp(p->nombre, m.texto))) u.nombre[i][c.cmd] = copiar(m.texto);
    usuario_guardar();
}

/* ------------------------------------------------------------------ dibujo */
void sonidos_dibujar(SDL_Renderer *r, int w, int h)
{
    if (!m.abierto) return;
    float k = fuente_factor(w, h);
    SDL_SetRenderScale(r, k, k);
    float vw = (float)w / k, vh = (float)h / k, cx = vw / 2;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, 215);
    SDL_RenderFillRect(r, NULL);
    char b[128];
    fuente_centrado(r, cx, 12, 3, TINTA_ORO, "MUSICA Y SONIDOS");

    /* pestañas */
    float ancho = 0;
    for (int c = 0; c < CAT_N; c++) ancho += fuente_ancho(CATEGORIA[c], 1.5f) + 24;
    float x = cx - ancho / 2 + 12;
    for (int c = 0; c < CAT_N; c++) {
        int es = c == m.categoria;
        if (es) {
            SDL_SetRenderDrawColor(r, 255, 190, 60, 50);
            SDL_FRect f = {x - 6, 42, fuente_ancho(CATEGORIA[c], 1.5f) + 12, 18};
            SDL_RenderFillRect(r, &f);
        }
        fuente_texto(r, x, 45, 1.5f, es ? TINTA_ORO : TINTA_GRIS, CATEGORIA[c]);
        x += fuente_ancho(CATEGORIA[c], 1.5f) + 24;
    }

    /* lista con desplazamiento: nombre, clave, duración y vibración */
    Clave l[256];
    int n = lista(m.categoria, l, 256), sel = m.sel[m.categoria];
    if (sel >= n) sel = n ? n - 1 : 0;
    const float L = 13, x0 = cx - 210, y0 = 80;
    int visibles = (int)((vh - y0 - 58) / L);
    if (visibles < 3) visibles = 3;
    int primero = sel - visibles / 2;
    if (primero > n - visibles) primero = n - visibles;
    if (primero < 0) primero = 0;
    if (!n) fuente_centrado(r, cx, y0 + 20, 1, TINTA_GRIS,
                            m.categoria == CAT_RECIENTES ? "Todavia no sono nada en el juego" : "Vacio");
    int con_vibracion = m.categoria != CAT_MUSICA;
    if (con_vibracion && n) fuente_texto(r, x0 + 372, y0 - 14, 1, TINTA_GRIS, "Vibra");
    for (int p = primero; p < n && p < primero + visibles; p++) {
        float y = y0 + (float)(p - primero) * L;
        Clave c = l[p];
        int es = p == sel, suena = m.sonando && c.modo == m.actual.modo && c.cmd == m.actual.cmd;
        int tinta = suena ? TINTA_VERDE : es ? TINTA_ORO : TINTA_PLATA;
        if (es) {
            SDL_SetRenderDrawColor(r, 255, 190, 60, 40);
            SDL_FRect f = {x0 - 6, y - 3, 432, L};
            SDL_RenderFillRect(r, &f);
            fuente_texto(r, x0 - 4, y, 1, TINTA_ORO, ">");
        }
        if (es && m.editando) {
            snprintf(b, sizeof b, "%s_", m.texto);
            fuente_texto(r, x0 + 8, y, 1, TINTA_VERDE, b);
        } else {
            fuente_texto(r, x0 + 8, y, 1, tinta, nombre(c.modo, c.cmd));
        }
        snprintf(b, sizeof b, "%02X:%02X", c.modo, c.cmd);            /* clave del driver */
        fuente_texto(r, x0 + 256, y, 1, TINTA_GRIS, b);
        const Sonido *s = perfil(c.modo, c.cmd);
        if (s) {
            if (s->decimas == 0xFFFF) snprintf(b, sizeof b, "bucle");
            else snprintf(b, sizeof b, "%d.%ds", s->decimas / 10, s->decimas % 10);
            fuente_texto(r, x0 + 356 - fuente_ancho(b, 1), y, 1, tinta, b);
        }
        if (con_vibracion && c.modo != MODO_MUSICA) {
            int i = modo_indice(c.modo), v = i >= 0 ? u.vib[i][c.cmd] : VIB_AUTO;
            const char *t = v != VIB_AUTO ? VIB_NOMBRE[v] : vibracion_de(c.modo, c.cmd) ? "auto" : "-";
            fuente_texto(r, x0 + 372, y, 1, v != VIB_AUTO ? TINTA_ORO : TINTA_GRIS, t);
        }
    }

    /* estado y ayuda */
    float y = vh - 46;
    if (m.sonando) {
        double seg = (double)(SDL_GetTicksNS() - m.desde) / 1e9;
        snprintf(b, sizeof b, "Sonando: %s  %02d:%02d", nombre(m.actual.modo, m.actual.cmd), (int)seg / 60, (int)seg % 60);
        fuente_centrado(r, cx, y, 1, TINTA_VERDE, b);
    }
    if (m.editando) {
        fuente_centrado(r, cx, y + 16, 1, TINTA_PLATA, "Escribe el nombre   Enter guardar   Esc cancelar");
    } else {
        fuente_centrado(r, cx, y + 16, 1, TINTA_GRIS, "Enter/A escuchar   Supr/Y parar   F2 renombrar");
        fuente_centrado(r, cx, y + 30, 1, TINTA_GRIS, "V/X vibracion   </> LB/RB pestana   Esc/B volver");
    }
    SDL_SetRenderScale(r, 1, 1);
}

/* ------------------------------------------------------------------ vibración */
static struct {
    SDL_Gamepad **mandos;
    int n_mandos, intensidad;
    uint8_t modo;
    const Vibracion *pedida;
    Uint64 fin;                                  /* fin de la vibración en curso (ns) */
    uint32_t fuerza;                             /* de la vibración en curso */
} v = {NULL, 0, 70, 0xFF, NULL, 0, 0};

static uint8_t *modo_driver(void) { return &v.modo; }

static void al_comando(uint8_t c)
{
    if (c == MODO_MUSICA || c == MODO_EFECTOS || c == MODO_VOCES) { v.modo = c; return; }
    if (c == 0x03) { v.modo = 0xFF; return; }
    if (m.abierto || v.modo == 0xFF || c < 0x20) return;
    anotar_reciente(v.modo, c);
    const Vibracion *p = vibracion_de(v.modo, c);
    if (p && (!v.pedida || p->baja > v.pedida->baja)) v.pedida = p;
}

void vibracion_iniciar(SDL_Gamepad **mandos, int n)
{
    v.mandos = mandos;
    v.n_mandos = n;
    ng_al_comando_sonido = al_comando;
}

void vibracion_intensidad(int intensidad) { v.intensidad = intensidad; }

void vibracion_cuadro(void)
{
    if (!v.pedida) return;
    const Vibracion *p = v.pedida;
    v.pedida = NULL;
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
