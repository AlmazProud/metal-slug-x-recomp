/* Menús del port (Fase 6C).
 *
 * Páginas apiladas; cada página es una lista de opciones (enum OP_*). Una opción puede abrir otra
 * página, ejecutar una acción o cambiar un valor (← / →). El texto usa la fuente del propio juego
 * (font.c) sobre el juego oscurecido:
 *   - Inicio: el demo del juego sigue corriendo de fondo, sin recibir las entradas;
 *   - Pausa: el juego queda detenido.
 */
#include "menu.h"

#include <stdio.h>
#include <string.h>

#include "debug.h"
#include "dispatcher.h"
#include "font.h"
#include "frontend.h"
#include "logo.h"
#include "sounds.h"

enum {
    OP_JUGAR, OP_CONTINUAR, OP_GUARDAR, OP_CARGAR, OP_CONFIG, OP_SONIDOS, OP_EXTRAS, OP_VOLVER_INICIO,
    OP_SALIR,
    /* configuración */
    OP_VIDEO, OP_AUDIO, OP_JUEGO, OP_CONTROLES,
    OP_PANTALLA, OP_VSYNC, OP_ASPECTO, OP_FILTRO, OP_SCANLINES,
    OP_VOLUMEN, OP_VIBRACION,
    OP_TURBO, OP_SANGRE, OP_AUTO1, OP_AUTO2, OP_AUTO_VEL, OP_PANTALLA_INICIO,
    OP_CONTROLES1, OP_CONTROLES2,
    /* extras */
    OP_DEBUG, OP_SERVICIO, OP_REINICIAR,
};

typedef struct { const char *titulo; const int *op; int n; } Pagina;

#define PAGINA(nombre, titulo, ...) \
    static const int nombre##_op[] = {__VA_ARGS__}; \
    static const Pagina nombre = {titulo, nombre##_op, (int)(sizeof nombre##_op / sizeof(int))}

PAGINA(P_INICIO, "METAL SLUG X", OP_JUGAR, OP_CARGAR, OP_CONFIG, OP_SONIDOS, OP_EXTRAS, OP_SALIR);
PAGINA(P_PAUSA, "PAUSA", OP_CONTINUAR, OP_GUARDAR, OP_CARGAR, OP_CONFIG, OP_SONIDOS, OP_EXTRAS,
       OP_VOLVER_INICIO, OP_SALIR);
PAGINA(P_CONFIG, "CONFIGURACION", OP_VIDEO, OP_AUDIO, OP_JUEGO, OP_CONTROLES);
PAGINA(P_VIDEO, "VIDEO", OP_PANTALLA, OP_VSYNC, OP_ASPECTO, OP_FILTRO, OP_SCANLINES);
PAGINA(P_AUDIO, "AUDIO", OP_VOLUMEN, OP_VIBRACION);
PAGINA(P_JUEGO, "JUEGO", OP_TURBO, OP_SANGRE, OP_AUTO1, OP_AUTO2, OP_AUTO_VEL, OP_PANTALLA_INICIO);
PAGINA(P_CONTROLES, "CONTROLES", OP_CONTROLES1, OP_CONTROLES2);
PAGINA(P_EXTRAS, "EXTRAS", OP_DEBUG, OP_SERVICIO, OP_REINICIAR);

static struct {
    const Pagina *pila[6];
    int sel[6];
    int nivel;                           /* -1 = cerrado */
    int remap_jugador, remap_accion;     /* -1 = sin captura de controles */
} m = {{0}, {0}, -1, -1, 0};

static int ciclar(int v, int d, int n) { return ((v + d) % n + n) % n; }

static void abrir(const Pagina *p)
{
    m.nivel = 0;
    m.pila[0] = p;
    m.sel[0] = 0;
}

void menu_inicio(void) { abrir(&P_INICIO); }
void menu_pausa(void) { abrir(&P_PAUSA); }
int menu_abierto(void) { return m.nivel >= 0; }
int menu_juego_corre(void) { return m.nivel >= 0 && m.pila[0] == &P_INICIO; }

static void cerrar(void)
{
    m.nivel = -1;
    fe_guardar_config();
}

static void entrar(const Pagina *p)
{
    if (m.nivel + 1 >= (int)(sizeof m.pila / sizeof m.pila[0])) return;
    m.nivel++;
    m.pila[m.nivel] = p;
    m.sel[m.nivel] = 0;
}

static void volver(void)
{
    if (m.nivel > 0) { m.nivel--; fe_guardar_config(); }
    else if (m.pila[0] == &P_PAUSA) cerrar();     /* en la pantalla de inicio, Esc no hace nada */
}

/* ------------------------------------------------------------------ textos */
static const char *etiqueta(int op)
{
    switch (op) {
    case OP_JUGAR: return "Jugar";
    case OP_CONTINUAR: return "Continuar";
    case OP_GUARDAR: return "Guardar partida";
    case OP_CARGAR: return "Cargar partida";
    case OP_CONFIG: return "Configuracion";
    case OP_SONIDOS: return "Musica y sonidos";
    case OP_EXTRAS: return "Extras";
    case OP_VOLVER_INICIO: return "Volver al inicio";
    case OP_SALIR: return "Salir";
    case OP_VIDEO: return "Video";
    case OP_AUDIO: return "Audio";
    case OP_JUEGO: return "Juego";
    case OP_CONTROLES: return "Controles";
    case OP_PANTALLA: return "Pantalla completa";
    case OP_VSYNC: return "V-sync";
    case OP_ASPECTO: return "Aspecto";
    case OP_FILTRO: return "Filtro";
    case OP_SCANLINES: return "Scanlines";
    case OP_VOLUMEN: return "Volumen";
    case OP_VIBRACION: return "Vibracion";
    case OP_TURBO: return "Ralentizacion";
    case OP_SANGRE: return "Sangre";
    case OP_AUTO1: return "Autodisparo J1";
    case OP_AUTO2: return "Autodisparo J2";
    case OP_AUTO_VEL: return "Velocidad autodisparo";
    case OP_PANTALLA_INICIO: return "Pantalla de inicio";
    case OP_CONTROLES1: return "Jugador 1";
    case OP_CONTROLES2: return "Jugador 2";
    case OP_DEBUG: return "Modo debug (F3)";
    case OP_SERVICIO: return "Menu de servicio";
    case OP_REINICIAR: return "Reiniciar la maquina";
    default: return "?";
    }
}

/* Valor a la derecha (vacío si la opción no tiene valor). */
static void valor(int op, char *b, size_t n)
{
    static const char *const ASPECTO[ASPECTO_N] = {"4:3", "Pixeles"};
    static const char *const FILTRO[FILTRO_N] = {"Nitido", "Suave", "Pixeles"};
    static const char *const AUTO[AUTO_N] = {"No", "Boton propio", "En el disparo"};
    static const char *const VEL[AUTO_VELOCIDADES] = {"10/s", "7.5/s", "5/s"};
    b[0] = 0;
    switch (op) {
    case OP_GUARDAR: case OP_CARGAR: snprintf(b, n, "< Ranura %d >", cfg.ranura); break;
    case OP_PANTALLA: snprintf(b, n, cfg.pantalla_completa ? "Si" : "No"); break;
    case OP_VSYNC: snprintf(b, n, cfg.vsync ? "Si" : "No"); break;
    case OP_ASPECTO: snprintf(b, n, "%s", ASPECTO[cfg.aspecto]); break;
    case OP_FILTRO: snprintf(b, n, "%s", FILTRO[cfg.filtro]); break;
    case OP_SCANLINES: snprintf(b, n, cfg.scanlines ? "Si" : "No"); break;
    case OP_VOLUMEN: snprintf(b, n, "%d%%", cfg.volumen); break;
    case OP_VIBRACION: if (cfg.vibracion) snprintf(b, n, "%d%%", cfg.vibracion); else snprintf(b, n, "No"); break;
    case OP_TURBO:
        if (cfg.turbo) snprintf(b, n, "No (x%d)", 1 << cfg.turbo);
        else snprintf(b, n, "Original");
        break;
    case OP_SANGRE: snprintf(b, n, cfg.sangre ? "Roja" : "Original"); break;
    case OP_AUTO1: case OP_AUTO2: snprintf(b, n, "%s", AUTO[cfg.autodisparo[op - OP_AUTO1]]); break;
    case OP_AUTO_VEL: snprintf(b, n, "%s", VEL[cfg.auto_velocidad]); break;
    case OP_PANTALLA_INICIO: snprintf(b, n, cfg.pantalla_inicio ? "Si" : "No"); break;
    case OP_DEBUG: snprintf(b, n, debug_activo() ? "Si" : "No"); break;
    case OP_CONFIG: case OP_EXTRAS: case OP_VIDEO: case OP_AUDIO: case OP_JUEGO: case OP_CONTROLES:
        snprintf(b, n, ">");
        break;
    default: break;
    }
}

static const char *ayuda(int op)
{
    switch (op) {
    case OP_TURBO: return "Original: el juego se ralentiza con muchos sprites, como la placa";
    case OP_ASPECTO: return "4:3 como el monitor original, o pixeles cuadrados";
    case OP_VSYNC: return "Sin cortes en la imagen; a 60 Hz el juego va 1.4% mas rapido";
    case OP_SANGRE: return "Original: la del menu de servicio (BLOOD, apagada en EE.UU.)";
    case OP_FILTRO: return "Nitido: pixeles parejos sin bordes borrosos";
    case OP_AUTO1: case OP_AUTO2: return "Boton propio: B / G en el teclado, RB en el mando";
    case OP_SERVICIO: return "Menu del BIOS: dificultad, vidas, tiempo (tambien con F2)";
    case OP_SONIDOS: return "Escucha la musica, las voces y los efectos del juego";
    case OP_DEBUG: return "Carga del 68000, ralentizacion, sprites, paleta, RAM";
    case OP_PANTALLA_INICIO: return "Mostrar este menu al abrir el juego";
    default: return "";
    }
}

/* ------------------------------------------------------------------ acciones */
static void cambiar_porcentaje(int *v, int d)
{
    *v += (d ? d : 1) * 10;
    if (*v > 100) *v = d ? 100 : 0;
    if (*v < 0) *v = 0;
}

/* d = 0: elegir; -1 / +1: cambiar el valor. */
static void accion(int op, int d)
{
    switch (op) {
    case OP_JUGAR: if (!d) { cerrar(); fe_jugar(); } break;
    case OP_CONTINUAR: if (!d) cerrar(); break;
    case OP_GUARDAR: case OP_CARGAR:
        if (d) cfg.ranura = ciclar(cfg.ranura - 1, d, 9) + 1;
        else {
            cerrar();
            if (op == OP_GUARDAR) fe_estado_guardar();
            else fe_estado_cargar();
        }
        break;
    case OP_CONFIG: if (!d) entrar(&P_CONFIG); break;
    case OP_SONIDOS: if (!d) sonidos_abrir(); break;
    case OP_EXTRAS: if (!d) entrar(&P_EXTRAS); break;
    case OP_VOLVER_INICIO: if (!d) { fe_reiniciar(); menu_inicio(); } break;
    case OP_SALIR: if (!d) fe_salir(); break;
    case OP_VIDEO: if (!d) entrar(&P_VIDEO); break;
    case OP_AUDIO: if (!d) entrar(&P_AUDIO); break;
    case OP_JUEGO: if (!d) entrar(&P_JUEGO); break;
    case OP_CONTROLES: if (!d) entrar(&P_CONTROLES); break;
    case OP_PANTALLA: cfg.pantalla_completa = !cfg.pantalla_completa; fe_aplicar_video(); break;
    case OP_VSYNC: cfg.vsync = !cfg.vsync; fe_aplicar_video(); break;
    case OP_ASPECTO: cfg.aspecto = ciclar(cfg.aspecto, d ? d : 1, ASPECTO_N); break;
    case OP_FILTRO: cfg.filtro = ciclar(cfg.filtro, d ? d : 1, FILTRO_N); break;
    case OP_SCANLINES: cfg.scanlines = !cfg.scanlines; break;
    case OP_VOLUMEN: cambiar_porcentaje(&cfg.volumen, d); fe_aplicar_volumen(); break;
    case OP_VIBRACION: cambiar_porcentaje(&cfg.vibracion, d); vibracion_intensidad(cfg.vibracion); break;
    case OP_TURBO: cfg.turbo = ciclar(cfg.turbo, d ? d : 1, 3); cpu_turbo = cfg.turbo; break;
    case OP_SANGRE: cfg.sangre = !cfg.sangre; fe_aplicar_sangre(); break;
    case OP_AUTO1: case OP_AUTO2:
        cfg.autodisparo[op - OP_AUTO1] = ciclar(cfg.autodisparo[op - OP_AUTO1], d ? d : 1, AUTO_N);
        break;
    case OP_AUTO_VEL: cfg.auto_velocidad = ciclar(cfg.auto_velocidad, d ? d : 1, AUTO_VELOCIDADES); break;
    case OP_PANTALLA_INICIO: cfg.pantalla_inicio = !cfg.pantalla_inicio; break;
    case OP_CONTROLES1: case OP_CONTROLES2:
        if (!d) { m.remap_jugador = op - OP_CONTROLES1; m.remap_accion = 0; }
        break;
    case OP_DEBUG: debug_activar(!debug_activo()); cfg.debug = debug_activo(); break;
    case OP_SERVICIO: if (!d) { cerrar(); fe_servicio(); } break;
    case OP_REINICIAR: if (!d) { cerrar(); fe_reiniciar(); } break;
    default: break;
    }
}

void menu_navegar(int t)
{
    if (m.nivel < 0) return;
    const Pagina *p = m.pila[m.nivel];
    int *s = &m.sel[m.nivel];
    switch (t) {
    case 0: *s = ciclar(*s, -1, p->n); break;
    case 1: *s = ciclar(*s, 1, p->n); break;
    case 2: accion(p->op[*s], -1); break;
    case 3: accion(p->op[*s], 1); break;
    case 4: accion(p->op[*s], 0); break;
    default: volver(); break;
    }
}

/* ------------------------------------------------------------------ controles */
int menu_capturando(void) { return m.remap_jugador >= 0; }

static void remap_siguiente(void)
{
    if (++m.remap_accion >= ACC_N) {
        m.remap_jugador = -1;
        fe_guardar_config();
        fe_avisar("Controles guardados");
    }
}

void menu_capturar_tecla(SDL_Scancode sc)
{
    if (sc == SDL_SCANCODE_ESCAPE) { m.remap_accion = ACC_N - 1; remap_siguiente(); }
    else if (sc == SDL_SCANCODE_DELETE) remap_siguiente();
    else { cfg.tecla[m.remap_jugador][m.remap_accion] = sc; remap_siguiente(); }
}

void menu_capturar_boton(SDL_GamepadButton b)
{
    cfg.boton[m.remap_jugador][m.remap_accion] = b;
    remap_siguiente();
}

/* ------------------------------------------------------------------ dibujo */
void menu_dibujar(SDL_Renderer *r, int w, int h)
{
    if (m.nivel < 0) return;
    /* Lienzo virtual de 480 x 300 ajustado a la ventana. */
    float k = fuente_factor(w, h);
    SDL_SetRenderScale(r, k, k);
    float vw = (float)w / k, vh = (float)h / k, cx = vw / 2;
    int inicio = m.pila[0] == &P_INICIO;
    SDL_SetRenderDrawBlendMode(r, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(r, 0, 0, 0, inicio && m.nivel == 0 ? 150 : 195);
    SDL_RenderFillRect(r, NULL);
    char b[128];

    if (m.remap_jugador >= 0) {
        float y = vh / 2 - 50;
        snprintf(b, sizeof b, "CONTROLES DEL JUGADOR %d", m.remap_jugador + 1);
        fuente_centrado(r, cx, y, 2, TINTA_ORO, b);
        snprintf(b, sizeof b, "Pulsa la tecla o el boton para:");
        fuente_centrado(r, cx, y + 30, 1, TINTA_PLATA, b);
        fuente_centrado(r, cx, y + 44, 2, TINTA_PLATA, ACCION_TEXTO[m.remap_accion]);
        const char *c = SDL_GetScancodeName(cfg.tecla[m.remap_jugador][m.remap_accion]);
        const char *bt = SDL_GetGamepadStringForButton(cfg.boton[m.remap_jugador][m.remap_accion]);
        snprintf(b, sizeof b, "Actual: %s / mando %s", *c ? c : "-", bt ? bt : "-");
        fuente_centrado(r, cx, y + 70, 1, TINTA_GRIS, b);
        fuente_centrado(r, cx, y + 90, 1, TINTA_GRIS, "Supr: dejar igual    Esc: terminar");
        SDL_SetRenderScale(r, 1, 1);
        return;
    }

    const Pagina *p = m.pila[m.nivel];
    float y;
    SDL_Texture *logo = logo_textura();
    if (inicio && m.nivel == 0 && logo) {
        float lw, lh;
        SDL_GetTextureSize(logo, &lw, &lh);
        float e = SDL_min(250 / lw, 116 / lh);                /* el logo del título, a lo ancho */
        SDL_FRect d = {SDL_roundf(cx - lw * e / 2), 6, lw * e, lh * e};
        SDL_RenderTexture(r, logo, NULL, &d);
        y = d.y + d.h + 14;
    } else if (inicio && m.nivel == 0) {
        fuente_centrado(r, cx, 34, fuente_escala_ajustada("METAL SLUG X", 5, vw - 24), TINTA_ORO, "METAL SLUG X");
        fuente_centrado(r, cx, 80, 1, TINTA_PLATA, "SUPER VEHICLE-001");
        y = 112;
    } else {
        fuente_centrado(r, cx, 26, 3, TINTA_ORO, p->titulo);
        y = 70;
    }
    /* panel de opciones */
    const float alto = 20, ancho = 360;
    float x0 = cx - ancho / 2;
    SDL_SetRenderDrawColor(r, 0, 0, 0, 140);
    SDL_FRect panel = {x0 - 10, y - 8, ancho + 20, p->n * alto + 12};
    SDL_RenderFillRect(r, &panel);
    for (int i = 0; i < p->n; i++) {
        int op = p->op[i], es = i == m.sel[m.nivel];
        float yi = y + i * alto;
        if (es) {
            SDL_SetRenderDrawColor(r, 255, 190, 60, 40);
            SDL_FRect barra = {x0 - 6, yi - 3, ancho + 12, 18};
            SDL_RenderFillRect(r, &barra);
            fuente_texto(r, x0 - 4, yi, 1.5f, TINTA_ORO, ">");
        }
        valor(op, b, sizeof b);
        if (b[0] && strcmp(b, ">")) {
            fuente_texto(r, x0 + 12, yi, 1.5f, es ? TINTA_ORO : TINTA_PLATA, etiqueta(op));
            fuente_texto(r, x0 + ancho - fuente_ancho(b, 1.5f), yi, 1.5f, es ? TINTA_ORO : TINTA_PLATA, b);
        } else if (inicio && m.nivel == 0) {
            fuente_centrado(r, cx, yi, 1.5f, es ? TINTA_ORO : TINTA_PLATA, etiqueta(op));
        } else {
            fuente_texto(r, x0 + 12, yi, 1.5f, es ? TINTA_ORO : TINTA_PLATA, etiqueta(op));
            if (b[0]) fuente_texto(r, x0 + ancho - 12, yi, 1.5f, TINTA_GRIS, b);
        }
    }
    /* ayuda de la opción y teclas */
    const char *a = ayuda(p->op[m.sel[m.nivel]]);
    if (a[0]) fuente_centrado(r, cx, vh - 36, 1, TINTA_PLATA, a);
    fuente_centrado(r, cx, vh - 18, 1, TINTA_GRIS,
                    inicio && m.nivel == 0 ? "Enter/A: elegir   </>: cambiar"
                                           : "Enter/A: elegir   </>: cambiar   Esc/B: volver");
    SDL_SetRenderScale(r, 1, 1);
}
