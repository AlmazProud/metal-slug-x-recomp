/* Modo con ventana (Fase 5): imagen, audio, controles de 2 jugadores, menú, estados guardados.
 *
 * Teclas fijas: Esc = menú, P = pausa, F5 / F9 = guardar / cargar estado en la ranura activa,
 * F11 o Alt+Enter = pantalla completa, F12 = captura, F2 = botón de test (menú de servicio del
 * BIOS: dificultad, vidas...), F3 = modo debug (debug.c). En el mando: Guide (o Back + Start)
 * abre el menú.
 *
 * Disparo automático: mientras se mantiene, el botón A se pulsa y se suelta solo, con un periodo
 * de 6, 8 o 12 cuadros. Medido en partida (comando de sonido F7 de la pistola): el juego no
 * dispara más de 10 veces por segundo aunque el patrón sea más rápido, así que 6 cuadros es el
 * máximo útil.
 * El resto de los controles se configura en el menú o en mslugx.ini (config.c).
 *
 * Imagen: la Neo Geo dibuja 320x224 píxeles que el monitor estira a 4:3, así que sus píxeles no
 * son cuadrados. El filtro "nítido" amplía primero por un factor entero sin suavizar y luego
 * ajusta al tamaño final con interpolación lineal: píxeles parejos sin bordes borrosos.
 */
#include <SDL3/SDL.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "debug.h"
#include "despachador.h"
#include "neogeo.h"
#include "port.h"
#include "sonidos.h"

void maquina_arrancar(const Opciones *o);

static Config cfg;
static char dir_pref[1024], ruta_cfg[1100];
static SDL_Window *ventana;
static SDL_Renderer *render;
static SDL_Texture *tex, *tex_grande;
static int escala_grande;
static SDL_AudioStream *audio;
static SDL_Gamepad *mandos[2];
static int test_cuadros;                     /* cuadros que falta mantener el botón de test */
static char aviso[256];
static Uint64 aviso_hasta;
static int salir;
static int avanzar;                          /* F6 en el modo debug: un cuadro */
static unsigned auto_fase[2];

static void avisar(const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(aviso, sizeof aviso, fmt, ap);
    va_end(ap);
    aviso_hasta = SDL_GetTicksNS() + 2500000000ULL;
}

/* ------------------------------------------------------------------ SRAM */
static void sram_ruta(char *r, size_t n) { snprintf(r, n, "%ssram.bin", dir_pref); }

static void sram_cargar(void)
{
    char r[1100];
    sram_ruta(r, sizeof r);
    FILE *f = fopen(r, "rb");
    if (f) {
        if (fread(ng.sram, 1, sizeof ng.sram, f) != sizeof ng.sram) memset(ng.sram, 0, sizeof ng.sram);
        fclose(f);
    }
}

static void sram_guardar(void)
{
    char r[1100];
    sram_ruta(r, sizeof r);
    FILE *f = fopen(r, "wb");
    if (f) { fwrite(ng.sram, 1, sizeof ng.sram, f); fclose(f); }
}

/* ------------------------------------------------------------------ estados guardados */
static void estado_ruta(char *r, size_t n) { snprintf(r, n, "%sestado_%d.bin", dir_pref, cfg.ranura); }

static void estado_guardar(void)
{
    size_t tam;
    uint8_t *d = ng_estado_guardar(&tam);
    char r[1100];
    estado_ruta(r, sizeof r);
    FILE *f = d ? fopen(r, "wb") : NULL;
    int ok = f && fwrite(d, 1, tam, f) == tam;
    if (f && fclose(f) != 0) ok = 0;
    free(d);
    avisar(ok ? "Estado guardado en la ranura %d" : "No se pudo guardar la ranura %d", cfg.ranura);
}

static void estado_cargar(void)
{
    char r[1100];
    estado_ruta(r, sizeof r);
    size_t tam;
    void *d = SDL_LoadFile(r, &tam);
    if (!d) { avisar("La ranura %d esta vacia", cfg.ranura); return; }
    if (ng_estado_cargar(d, tam) == 0) avisar("Estado de la ranura %d cargado", cfg.ranura);
    else avisar("La ranura %d es de otra version del port", cfg.ranura);
    SDL_free(d);
}

/* ------------------------------------------------------------------ entradas */
static void leer_entradas(void)
{
    const bool *k = SDL_GetKeyboardState(NULL);
    for (int j = 0; j < 2; j++) {
        uint8_t joy = 0, sis = 0;
        static const uint8_t BIT_JOY[8] = {NG_ARRIBA, NG_ABAJO, NG_IZQUIERDA, NG_DERECHA, NG_A, NG_B, NG_C, NG_D};
        int pulsado[ACC_N] = {0};
        for (int a = 0; a < ACC_N; a++) {
            if (cfg.tecla[j][a] != SDL_SCANCODE_UNKNOWN && k[cfg.tecla[j][a]]) pulsado[a] = 1;
            if (mandos[j] && cfg.boton[j][a] != SDL_GAMEPAD_BUTTON_INVALID &&
                SDL_GetGamepadButton(mandos[j], cfg.boton[j][a])) pulsado[a] = 1;
        }
        if (mandos[j]) {                       /* la palanca izquierda siempre mueve */
            int ax = SDL_GetGamepadAxis(mandos[j], SDL_GAMEPAD_AXIS_LEFTX);
            int ay = SDL_GetGamepadAxis(mandos[j], SDL_GAMEPAD_AXIS_LEFTY);
            if (ay < -16000) pulsado[ACC_ARRIBA] = 1;
            if (ay > 16000) pulsado[ACC_ABAJO] = 1;
            if (ax < -16000) pulsado[ACC_IZQUIERDA] = 1;
            if (ax > 16000) pulsado[ACC_DERECHA] = 1;
        }
        /* Disparo automático: la mitad del periodo pulsado. */
        int modo = cfg.autodisparo[j];
        int auto_on = (modo == AUTO_BOTON && pulsado[ACC_AUTO]) || (modo == AUTO_DISPARO && pulsado[ACC_A]);
        if (auto_on) {
            unsigned periodo = (unsigned)AUTO_PERIODO[cfg.auto_velocidad];
            int pulsar = auto_fase[j]++ % periodo < periodo / 2;
            if (modo == AUTO_DISPARO) pulsado[ACC_A] = pulsar;
            else if (pulsar) pulsado[ACC_A] = 1;
        } else {
            auto_fase[j] = 0;
        }
        for (int a = 0; a < 8; a++) if (pulsado[a]) joy |= BIT_JOY[a];
        if (pulsado[ACC_START]) sis |= NG_START;
        if (pulsado[ACC_MONEDA]) sis |= NG_MONEDA;
        /* Una dirección y su opuesta a la vez no existen en una palanca real. */
        if ((joy & (NG_ARRIBA | NG_ABAJO)) == (NG_ARRIBA | NG_ABAJO)) joy &= ~(NG_ARRIBA | NG_ABAJO);
        if ((joy & (NG_IZQUIERDA | NG_DERECHA)) == (NG_IZQUIERDA | NG_DERECHA)) joy &= ~(NG_IZQUIERDA | NG_DERECHA);
        ng.joy[j] = joy;
        ng.sistema[j] = sis;
    }
    ng.test = (k[SDL_SCANCODE_F2] || test_cuadros > 0) ? 0x00 : 0x80;
    if (test_cuadros > 0) test_cuadros--;
}

static void mando_conectado(SDL_JoystickID id)
{
    for (int j = 0; j < 2; j++)
        if (!mandos[j]) {
            mandos[j] = SDL_OpenGamepad(id);
            if (mandos[j]) avisar("Mando %d: %s", j + 1, SDL_GetGamepadName(mandos[j]));
            return;
        }
}

static void mando_desconectado(SDL_JoystickID id)
{
    for (int j = 0; j < 2; j++)
        if (mandos[j] && SDL_GetGamepadID(mandos[j]) == id) {
            SDL_CloseGamepad(mandos[j]);
            mandos[j] = NULL;
            avisar("Mando %d desconectado", j + 1);
        }
}

/* ------------------------------------------------------------------ menú */
enum {
    M_CONTINUAR, M_GUARDAR, M_CARGAR, M_TURBO, M_PANTALLA, M_ASPECTO, M_FILTRO, M_SCANLINES,
    M_VOLUMEN, M_SONIDOS, M_VIBRACION, M_AUTO1, M_AUTO2, M_AUTO_VEL, M_CONTROLES1, M_CONTROLES2,
    M_SERVICIO, M_DEBUG, M_REINICIAR, M_SALIR, M_N
};
static int menu_abierto, menu_sel;
static int remap_jugador = -1, remap_accion;     /* configuración de controles en curso */

static void aplicar_video(void)
{
    SDL_SetWindowFullscreen(ventana, cfg.pantalla_completa != 0);
}

static void texto_item(int i, char *b, size_t n)
{
    static const char *const ASPECTO[ASPECTO_N] = {"4:3 (monitor)", "pixeles cuadrados"};
    static const char *const FILTRO[FILTRO_N] = {"nitido", "suave", "pixeles"};
    switch (i) {
    case M_CONTINUAR: snprintf(b, n, "Continuar"); break;
    case M_GUARDAR: snprintf(b, n, "Guardar estado    < ranura %d >", cfg.ranura); break;
    case M_CARGAR: snprintf(b, n, "Cargar estado     < ranura %d >", cfg.ranura); break;
    case M_TURBO:
        if (cfg.turbo) snprintf(b, n, "Ralentizacion     no (68000 x%d)", 1 << cfg.turbo);
        else snprintf(b, n, "Ralentizacion     si (original)");
        break;
    case M_PANTALLA: snprintf(b, n, "Pantalla completa %s", cfg.pantalla_completa ? "si" : "no"); break;
    case M_ASPECTO: snprintf(b, n, "Aspecto           %s", ASPECTO[cfg.aspecto]); break;
    case M_FILTRO: snprintf(b, n, "Filtro            %s", FILTRO[cfg.filtro]); break;
    case M_SCANLINES: snprintf(b, n, "Scanlines         %s", cfg.scanlines ? "si" : "no"); break;
    case M_VOLUMEN: snprintf(b, n, "Volumen           %d%%", cfg.volumen); break;
    case M_AUTO1: case M_AUTO2: {
        static const char *const AUTO[AUTO_N] = {"no", "boton propio", "en el disparo"};
        snprintf(b, n, "Autodisparo J%d    %s", i - M_AUTO1 + 1, AUTO[cfg.autodisparo[i - M_AUTO1]]);
        break;
    }
    case M_AUTO_VEL: {
        static const char *const VEL[AUTO_VELOCIDADES] = {"10", "7.5", "5"};
        snprintf(b, n, "Autodisparo       %s por segundo", VEL[cfg.auto_velocidad]);
        break;
    }
    case M_DEBUG: snprintf(b, n, "Modo debug (F3)   %s", debug_activo() ? "si" : "no"); break;
    case M_SONIDOS: snprintf(b, n, "Musica y sonidos"); break;
    case M_VIBRACION:
        if (cfg.vibracion) snprintf(b, n, "Vibracion         %d%%", cfg.vibracion);
        else snprintf(b, n, "Vibracion         no");
        break;
    case M_CONTROLES1: snprintf(b, n, "Controles del jugador 1"); break;
    case M_CONTROLES2: snprintf(b, n, "Controles del jugador 2"); break;
    case M_SERVICIO: snprintf(b, n, "Menu de servicio (dificultad, vidas)"); break;
    case M_REINICIAR: snprintf(b, n, "Reiniciar la maquina"); break;
    default: snprintf(b, n, "Salir"); break;
    }
}

static int ciclar(int v, int d, int n) { return ((v + d) % n + n) % n; }

/* d = 0: confirmar; -1 / +1: cambiar el valor. */
static void menu_accion(int i, int d)
{
    switch (i) {
    case M_CONTINUAR: if (!d) menu_abierto = 0; break;
    case M_GUARDAR: case M_CARGAR:
        if (d) cfg.ranura = ciclar(cfg.ranura - 1, d, 9) + 1;
        else { if (i == M_GUARDAR) estado_guardar(); else estado_cargar(); menu_abierto = 0; }
        break;
    case M_TURBO:
        cfg.turbo = ciclar(cfg.turbo, d ? d : 1, 3);
        cpu_turbo = cfg.turbo;
        break;
    case M_PANTALLA: cfg.pantalla_completa = !cfg.pantalla_completa; aplicar_video(); break;
    case M_ASPECTO: cfg.aspecto = ciclar(cfg.aspecto, d ? d : 1, ASPECTO_N); break;
    case M_FILTRO: cfg.filtro = ciclar(cfg.filtro, d ? d : 1, FILTRO_N); break;
    case M_SCANLINES: cfg.scanlines = !cfg.scanlines; break;
    case M_VOLUMEN:
        cfg.volumen += (d ? d : 1) * 10;
        if (cfg.volumen > 100) cfg.volumen = d ? 100 : 0;
        if (cfg.volumen < 0) cfg.volumen = 0;
        if (audio) SDL_SetAudioStreamGain(audio, cfg.volumen / 100.0f);
        break;
    case M_AUTO1: case M_AUTO2:
        cfg.autodisparo[i - M_AUTO1] = ciclar(cfg.autodisparo[i - M_AUTO1], d ? d : 1, AUTO_N);
        break;
    case M_AUTO_VEL: cfg.auto_velocidad = ciclar(cfg.auto_velocidad, d ? d : 1, AUTO_VELOCIDADES); break;
    case M_DEBUG: debug_activar(!debug_activo()); cfg.debug = debug_activo(); break;
    case M_SONIDOS: if (!d) { menu_abierto = 0; sonidos_abrir(); } break;
    case M_VIBRACION:
        cfg.vibracion += (d ? d : 1) * 10;
        if (cfg.vibracion > 100) cfg.vibracion = d ? 100 : 0;
        if (cfg.vibracion < 0) cfg.vibracion = 0;
        vibracion_intensidad(cfg.vibracion);
        break;
    case M_CONTROLES1: case M_CONTROLES2:
        if (!d) { remap_jugador = i - M_CONTROLES1; remap_accion = 0; }
        break;
    case M_SERVICIO: if (!d) { test_cuadros = 30; menu_abierto = 0; } break;
    case M_REINICIAR:
        if (!d) {
            uint8_t sram[sizeof ng.sram];
            memcpy(sram, ng.sram, sizeof sram);
            ng_encender();
            memcpy(ng.sram, sram, sizeof sram);
            menu_abierto = 0;
            avisar("Maquina reiniciada");
        }
        break;
    default: if (!d) salir = 1; break;
    }
}

static void remap_siguiente(void)
{
    if (++remap_accion >= ACC_N) {
        remap_jugador = -1;
        config_guardar(&cfg, ruta_cfg);
        avisar("Controles guardados");
    }
}

/* Teclas de navegación del menú (teclado y mando): 0 = arriba, 1 = abajo, 2 = izq., 3 = der.,
   4 = confirmar, 5 = volver. */
static void menu_navegar(int t)
{
    if (t == 0) menu_sel = ciclar(menu_sel, -1, M_N);
    else if (t == 1) menu_sel = ciclar(menu_sel, 1, M_N);
    else if (t == 2) menu_accion(menu_sel, -1);
    else if (t == 3) menu_accion(menu_sel, 1);
    else if (t == 4) menu_accion(menu_sel, 0);
    else menu_abierto = 0;
    if (!menu_abierto) config_guardar(&cfg, ruta_cfg);
}

/* ------------------------------------------------------------------ dibujo */
static SDL_FRect destino(int w, int h)
{
    double ar = cfg.aspecto == ASPECTO_4_3 ? 4.0 / 3.0 : (double)NG_ANCHO / NG_ALTO;
    double dw = w, dh = w / ar;
    if (dh > h) { dh = h; dw = h * ar; }
    if (cfg.filtro == FILTRO_PIXELES && cfg.aspecto == ASPECTO_PIXELES) {
        int s = (int)(dh / NG_ALTO);         /* escala entera exacta si cabe */
        if (s >= 1) { dw = NG_ANCHO * s; dh = NG_ALTO * s; }
    }
    SDL_FRect r = {(float)((w - dw) / 2), (float)((h - dh) / 2), (float)dw, (float)dh};
    r.x = SDL_roundf(r.x); r.y = SDL_roundf(r.y);
    return r;
}

static SDL_FRect dibujar_juego(int w, int h)
{
    SDL_UpdateTexture(tex, NULL, ng.pantalla, NG_ANCHO * 4);
    SDL_FRect d = destino(w, h);
    if (cfg.filtro == FILTRO_NITIDO) {
        int s = (int)SDL_ceil(SDL_max(d.w / NG_ANCHO, d.h / NG_ALTO));
        s = s < 1 ? 1 : s > 8 ? 8 : s;
        if (s != escala_grande) {
            if (tex_grande) SDL_DestroyTexture(tex_grande);
            tex_grande = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                           NG_ANCHO * s, NG_ALTO * s);
            SDL_SetTextureScaleMode(tex_grande, SDL_SCALEMODE_LINEAR);
            escala_grande = s;
        }
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
        SDL_SetRenderTarget(render, tex_grande);
        SDL_RenderTexture(render, tex, NULL, NULL);
        SDL_SetRenderTarget(render, NULL);
        SDL_RenderTexture(render, tex_grande, NULL, &d);
    } else {
        SDL_SetTextureScaleMode(tex, cfg.filtro == FILTRO_SUAVE ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        SDL_RenderTexture(render, tex, NULL, &d);
    }
    if (cfg.scanlines && d.h >= NG_ALTO * 2) {
        static SDL_FRect lineas[NG_ALTO];
        float paso = d.h / NG_ALTO;
        for (int y = 0; y < NG_ALTO; y++)
            lineas[y] = (SDL_FRect){d.x, d.y + y * paso + paso * 0.55f, d.w, paso * 0.45f};
        SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(render, 0, 0, 0, 110);
        SDL_RenderFillRects(render, lineas, NG_ALTO);
    }
    return d;
}

static void texto(float x, float y, const char *s, Uint8 r, Uint8 g, Uint8 b)
{
    SDL_SetRenderDrawColor(render, r, g, b, 255);
    SDL_RenderDebugText(render, x, y, s);
}

static void dibujar_interfaz(int w, int h)
{
    int k = SDL_max(1, SDL_min(w / 360, h / 250));
    SDL_SetRenderScale(render, (float)k, (float)k);
    float vw = (float)w / k, vh = (float)h / k;
    const float L = 12;                                  /* alto de línea (fuente de 8x8) */
    if (menu_abierto) {
        SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(render, 0, 0, 0, 190);
        SDL_RenderFillRect(render, NULL);
        char b[128];
        if (remap_jugador >= 0) {
            float y = vh / 2 - 3 * L;
            snprintf(b, sizeof b, "Controles del jugador %d", remap_jugador + 1);
            texto(vw / 2 - strlen(b) * 4.0f, y, b, 255, 210, 80);
            snprintf(b, sizeof b, "Pulsa la tecla o el boton para: %s", ACCION_TEXTO[remap_accion]);
            texto(vw / 2 - strlen(b) * 4.0f, y + 2 * L, b, 255, 255, 255);
            const char *c = SDL_GetScancodeName(cfg.tecla[remap_jugador][remap_accion]);
            const char *m = SDL_GetGamepadStringForButton(cfg.boton[remap_jugador][remap_accion]);
            snprintf(b, sizeof b, "Actual: %s / mando %s", *c ? c : "-", m ? m : "-");
            texto(vw / 2 - strlen(b) * 4.0f, y + 3 * L, b, 170, 170, 170);
            snprintf(b, sizeof b, "Supr: dejar igual   Esc: terminar");
            texto(vw / 2 - strlen(b) * 4.0f, y + 5 * L, b, 170, 170, 170);
        } else {
            float y = vh / 2 - (M_N + 2) * L / 2;
            texto(vw / 2 - 52, y, "METAL SLUG X", 255, 210, 80);
            for (int i = 0; i < M_N; i++) {
                texto_item(i, b, sizeof b);
                int sel = i == menu_sel;
                texto(vw / 2 - 150, y + (i + 2) * L, sel ? ">" : " ", 255, 210, 80);
                texto(vw / 2 - 138, y + (i + 2) * L, b, sel ? 255 : 190, sel ? 255 : 190, sel ? 255 : 190);
            }
        }
    }
    if (aviso[0] && SDL_GetTicksNS() < aviso_hasta) {
        SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(render, 0, 0, 0, 160);
        SDL_FRect f = {4, vh - L - 6, strlen(aviso) * 8.0f + 8, L + 2};
        SDL_RenderFillRect(render, &f);
        texto(8, vh - L - 3, aviso, 255, 255, 255);
    }
    SDL_SetRenderScale(render, 1, 1);
}

/* ------------------------------------------------------------------ eventos */
static void tecla(const SDL_KeyboardEvent *e, int *pausa, int *capturas)
{
    SDL_Scancode sc = e->scancode;
    if (remap_jugador >= 0) {
        if (e->repeat) return;
        if (sc == SDL_SCANCODE_ESCAPE) { remap_accion = ACC_N - 1; remap_siguiente(); }
        else if (sc == SDL_SCANCODE_DELETE) remap_siguiente();
        else { cfg.tecla[remap_jugador][remap_accion] = sc; remap_siguiente(); }
        return;
    }
    if (sc == SDL_SCANCODE_F11 || (sc == SDL_SCANCODE_RETURN && (e->mod & SDL_KMOD_ALT))) {
        cfg.pantalla_completa = !cfg.pantalla_completa;
        aplicar_video();
        return;
    }
    if (sonidos_abierto()) {
        switch (sc) {
        case SDL_SCANCODE_UP: sonidos_navegar(0); break;
        case SDL_SCANCODE_DOWN: sonidos_navegar(1); break;
        case SDL_SCANCODE_LEFT: sonidos_navegar(2); break;
        case SDL_SCANCODE_RIGHT: sonidos_navegar(3); break;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_SPACE: case SDL_SCANCODE_KP_ENTER: sonidos_navegar(4); break;
        case SDL_SCANCODE_ESCAPE: sonidos_navegar(5); break;
        case SDL_SCANCODE_DELETE: case SDL_SCANCODE_BACKSPACE: sonidos_navegar(6); break;
        default: break;
        }
        return;
    }
    if (menu_abierto) {
        switch (sc) {
        case SDL_SCANCODE_UP: menu_navegar(0); break;
        case SDL_SCANCODE_DOWN: menu_navegar(1); break;
        case SDL_SCANCODE_LEFT: menu_navegar(2); break;
        case SDL_SCANCODE_RIGHT: menu_navegar(3); break;
        case SDL_SCANCODE_RETURN: case SDL_SCANCODE_SPACE: case SDL_SCANCODE_KP_ENTER: menu_navegar(4); break;
        case SDL_SCANCODE_ESCAPE: menu_navegar(5); break;
        default: break;
        }
        return;
    }
    int dbg = debug_tecla(e);
    if (dbg == DEBUG_AVANZAR) { *pausa = 1; avanzar = 1; }
    if (dbg != DEBUG_NADA) { cfg.debug = debug_activo(); return; }
    switch (sc) {
    case SDL_SCANCODE_ESCAPE: menu_abierto = 1; break;
    case SDL_SCANCODE_P: *pausa = !*pausa; avisar(*pausa ? "Pausa" : "Continuar"); break;
    case SDL_SCANCODE_F5: estado_guardar(); break;
    case SDL_SCANCODE_F9: estado_cargar(); break;
    case SDL_SCANCODE_F12: {
        char r[1100];
        snprintf(r, sizeof r, "%scaptura_%03d.bmp", dir_pref, (*capturas)++);
        if (guardar_bmp(r) == 0) avisar("Captura: %s", r);
        break;
    }
    default: break;
    }
}

static void boton(const SDL_GamepadButtonEvent *e)
{
    SDL_GamepadButton b = (SDL_GamepadButton)e->button;
    if (remap_jugador >= 0) {
        cfg.boton[remap_jugador][remap_accion] = b;
        remap_siguiente();
        return;
    }
    SDL_Gamepad *g = SDL_GetGamepadFromID(e->which);
    int combo = g && ((b == SDL_GAMEPAD_BUTTON_START && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_BACK)) ||
                      (b == SDL_GAMEPAD_BUTTON_BACK && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START)));
    if (sonidos_abierto()) {
        switch (b) {
        case SDL_GAMEPAD_BUTTON_DPAD_UP: sonidos_navegar(0); break;
        case SDL_GAMEPAD_BUTTON_DPAD_DOWN: sonidos_navegar(1); break;
        case SDL_GAMEPAD_BUTTON_DPAD_LEFT: case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: sonidos_navegar(2); break;
        case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: sonidos_navegar(3); break;
        case SDL_GAMEPAD_BUTTON_SOUTH: sonidos_navegar(4); break;
        case SDL_GAMEPAD_BUTTON_NORTH: sonidos_navegar(6); break;
        case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_GUIDE: sonidos_navegar(5); break;
        default: break;
        }
        return;
    }
    if (b == SDL_GAMEPAD_BUTTON_GUIDE || combo) {
        if (menu_abierto) menu_navegar(5);
        else menu_abierto = 1;
        return;
    }
    if (!menu_abierto) return;
    switch (b) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: menu_navegar(0); break;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: menu_navegar(1); break;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: menu_navegar(2); break;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: menu_navegar(3); break;
    case SDL_GAMEPAD_BUTTON_SOUTH: menu_navegar(4); break;
    case SDL_GAMEPAD_BUTTON_EAST: menu_navegar(5); break;
    default: break;
    }
}

/* ------------------------------------------------------------------ ROMs */
static volatile int dialogo_listo;
static char dialogo_ruta[1024];

static void SDLCALL al_elegir_carpeta(void *datos, const char *const *lista, int filtro)
{
    (void)datos; (void)filtro;
    if (lista && lista[0]) snprintf(dialogo_ruta, sizeof dialogo_ruta, "%s", lista[0]);
    dialogo_listo = 1;
}

static int probar_roms(const char *carpeta, char *error, int tam)
{
    if (ng_roms_cargar(&ng_roms, carpeta, error, tam) == 0) {
        snprintf(cfg.roms, sizeof cfg.roms, "%s", carpeta);
        return 1;
    }
    char sub[1100];                          /* también vale la carpeta raíz del juego */
    snprintf(sub, sizeof sub, "%s\\resources\\game", carpeta);
    if (ng_roms_cargar(&ng_roms, sub, error, tam) == 0) {
        snprintf(cfg.roms, sizeof cfg.roms, "%s", sub);
        return 1;
    }
    return 0;
}

/* --roms, la carpeta recordada, "roms" junto al ejecutable, la instalación de GOG y, si nada
   sirve, la carpeta que elija el usuario. */
static int buscar_roms(const Opciones *o)
{
    char error[1200] = "", carpeta[1024];
    if (o->roms) {
        if (probar_roms(o->roms, error, sizeof error)) return 1;
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Metal Slug X", error, NULL);
        return 0;
    }
    if (cfg.roms[0] && probar_roms(cfg.roms, error, sizeof error)) return 1;
    const char *base = SDL_GetBasePath();
    snprintf(carpeta, sizeof carpeta, "%sroms", base ? base : "");
    if (probar_roms(carpeta, error, sizeof error)) return 1;
    if (gog_buscar(carpeta, sizeof carpeta) && probar_roms(carpeta, error, sizeof error)) return 1;
    for (;;) {
        char msg[1600];
        snprintf(msg, sizeof msg,
                 "No se encontró una copia válida de Metal Slug X de GOG.\n\n%s\n\n"
                 "Elige la carpeta donde instalaste el juego (o su subcarpeta resources\\game).", error);
        SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_INFORMATION, "Metal Slug X", msg, NULL);
        dialogo_listo = 0;
        dialogo_ruta[0] = 0;
        SDL_ShowOpenFolderDialog(al_elegir_carpeta, NULL, NULL, NULL, false);
        while (!dialogo_listo) {
            SDL_Event e;
            SDL_WaitEventTimeout(&e, 50);
        }
        if (!dialogo_ruta[0]) return 0;          /* cancelado */
        if (probar_roms(dialogo_ruta, error, sizeof error)) return 1;
    }
}

/* ------------------------------------------------------------------ principal */
int ventana_correr(const Opciones *o)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | (o->sin_audio ? 0 : SDL_INIT_AUDIO))) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    /* PD_PREF: otra carpeta de preferencias (pruebas, sin tocar la configuración del usuario). */
    char *pref = getenv("PD_PREF") ? NULL : SDL_GetPrefPath("ProjectoDecompile", "mslugx");
    snprintf(dir_pref, sizeof dir_pref, "%s", pref ? pref : getenv("PD_PREF") ? getenv("PD_PREF") : "");
    SDL_free(pref);
    snprintf(ruta_cfg, sizeof ruta_cfg, "%smslugx.ini", dir_pref);
    config_por_defecto(&cfg);
    config_cargar(&cfg, ruta_cfg);
    if (o->escala > 0) cfg.escala = o->escala;
    if (o->turbo >= 0) cfg.turbo = o->turbo;

    if (!buscar_roms(o)) { SDL_Quit(); return 1; }
    config_guardar(&cfg, ruta_cfg);

    ng_audio_activo = !o->sin_audio;
    cpu_turbo = cfg.turbo;
    maquina_arrancar(o);
    debug_activar(cfg.debug);
    vibracion_iniciar(mandos, 2);
    vibracion_intensidad(cfg.vibracion);
    if (!o->sram_vacia) sram_cargar();

    int alto = NG_ALTO * cfg.escala;
    int ancho = cfg.aspecto == ASPECTO_4_3 ? alto * 4 / 3 : NG_ANCHO * cfg.escala;
    if (!SDL_CreateWindowAndRenderer("Metal Slug X", ancho, alto, SDL_WINDOW_RESIZABLE, &ventana, &render)) {
        fprintf(stderr, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(ventana, NG_ANCHO, NG_ALTO);
    tex = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, NG_ANCHO, NG_ALTO);
    aplicar_video();

    /* Audio: SDL convierte de 55 555 Hz a la frecuencia del dispositivo. */
    if (ng_audio_activo) {
        const SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, NG_AUDIO_HZ};
        audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (audio) {
            /* Colchón inicial de 50 ms de silencio contra las variaciones del ritmo del video. */
            static const int16_t silencio[NG_AUDIO_HZ / 20 * 2];
            SDL_PutAudioStreamData(audio, silencio, (int)sizeof silencio);
            SDL_SetAudioStreamGain(audio, cfg.volumen / 100.0f);
            SDL_ResumeAudioStreamDevice(audio);
        }
        else { fprintf(stderr, "Sin audio: %s\n", SDL_GetError()); ng_audio_activo = 0; }
    }
    static int16_t muestras[4096 * 2];
    uint64_t audio_cuadros = 0, audio_descartes = 0, audio_vacios = 0, audio_cola_suma = 0;
    if (cfg.turbo) avisar("Ralentizacion desactivada (68000 x%d). Esc: menu", 1 << cfg.turbo);
    else avisar("Esc: menu   F5/F9: guardar/cargar estado");

    /* PD_PRUEBA_VENTANA=N,archivo.png[,menu|sonidos]: tras N cuadros guarda lo que muestra la
       ventana (con el menú, o con el menú de sonido reproduciendo la segunda música) y sale. */
    long prueba_cuadros = 0;
    char prueba_png[1024] = "";
    int prueba_menu = 0, prueba_sonidos = 0;
    if (getenv("PD_PRUEBA_VENTANA")) {
        char extra[16] = "";
        sscanf(getenv("PD_PRUEBA_VENTANA"), "%ld,%1023[^,],%15s", &prueba_cuadros, prueba_png, extra);
        prueba_menu = !strcmp(extra, "menu");
        prueba_sonidos = !strcmp(extra, "sonidos");
    }
    long cuadros_vistos = 0;

    int pausa = 0, capturas = 0;
    Uint64 proximo = SDL_GetTicksNS();
    while (!salir) {
        if (prueba_cuadros && prueba_menu && cuadros_vistos == prueba_cuadros - 1) menu_abierto = 1;
        if (prueba_cuadros && prueba_sonidos && cuadros_vistos == prueba_cuadros - 60) {
            sonidos_abrir();
            sonidos_navegar(1);
            sonidos_navegar(4);
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT: salir = 1; break;
            case SDL_EVENT_KEY_DOWN: if (!e.key.repeat || menu_abierto) tecla(&e.key, &pausa, &capturas); break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: boton(&e.gbutton); break;
            case SDL_EVENT_GAMEPAD_ADDED: mando_conectado(e.gdevice.which); break;
            case SDL_EVENT_GAMEPAD_REMOVED: mando_desconectado(e.gdevice.which); break;
            case SDL_EVENT_WINDOW_ENTER_FULLSCREEN: cfg.pantalla_completa = 1; break;
            case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: cfg.pantalla_completa = 0; break;
            default: break;
            }
        }
        if (sonidos_abierto()) {
            sonidos_cuadro();
        } else if ((!pausa || avanzar) && !menu_abierto) {
            leer_entradas();
            ng_ejecutar_cuadro();
            debug_cuadro();
            vibracion_cuadro();
            avanzar = 0;
        }
        if (audio) {
            uint32_t n = ng_audio_leer(muestras, 4096);
            /* Si el dispositivo se atrasa (más de ~150 ms en cola), se descarta para no acumular
               latencia; el ritmo lo marca el video a 59,1856 Hz. */
            int cola = SDL_GetAudioStreamQueued(audio);
            audio_cuadros++;
            audio_cola_suma += (uint64_t)(cola > 0 ? cola : 0);
            if (cola == 0) audio_vacios++;
            if (cola < NG_AUDIO_HZ * 4 / 100) {           /* < 10 ms: rellena para no cortar */
                static const int16_t relleno[NG_AUDIO_HZ / 50 * 2];
                SDL_PutAudioStreamData(audio, relleno, (int)sizeof relleno);
            }
            if (n && cola < NG_AUDIO_HZ * 4 * 15 / 100)
                SDL_PutAudioStreamData(audio, muestras, (int)(n * 4));
            else if (n)
                audio_descartes++;
        }
        int w, h;
        SDL_GetRenderOutputSize(render, &w, &h);
        SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
        SDL_RenderClear(render);
        SDL_FRect juego = dibujar_juego(w, h);
        debug_dibujar(render, juego, w, h, cfg.turbo);
        dibujar_interfaz(w, h);
        sonidos_dibujar(render, w, h);
        if (prueba_cuadros && ++cuadros_vistos >= prueba_cuadros) {
            SDL_Surface *s = SDL_RenderReadPixels(render, NULL);
            if (s) { SDL_SavePNG(s, prueba_png); SDL_DestroySurface(s); }
            salir = 1;
        }
        SDL_RenderPresent(render);

        proximo += NS_POR_CUADRO;
        Uint64 ahora = SDL_GetTicksNS();
        if (prueba_cuadros) proximo = ahora;                              /* pruebas: sin esperar */
        else if (proximo > ahora) SDL_DelayPrecise(proximo - ahora);
        else if (ahora - proximo > 4 * NS_POR_CUADRO) proximo = ahora;   /* nos atrasamos: no acumular */
    }

    sonidos_cerrar();                    /* la SRAM y el estado vuelven a ser los del juego */
    if (!o->sram_vacia) sram_guardar();
    config_guardar(&cfg, ruta_cfg);
    if (audio && ng_traza && audio_cuadros)
        fprintf(ng_traza, "audio: %llu cuadros, cola media %.1f ms, cola vacía %llu veces, descartes %llu\n",
                (unsigned long long)audio_cuadros, audio_cola_suma / (double)audio_cuadros / (NG_AUDIO_HZ * 4) * 1000,
                (unsigned long long)audio_vacios, (unsigned long long)audio_descartes);
    if (audio) SDL_DestroyAudioStream(audio);
    for (int j = 0; j < 2; j++) if (mandos[j]) SDL_CloseGamepad(mandos[j]);
    if (tex_grande) SDL_DestroyTexture(tex_grande);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(render);
    SDL_DestroyWindow(ventana);
    SDL_Quit();
    ng_roms_liberar(&ng_roms);
    return 0;
}
