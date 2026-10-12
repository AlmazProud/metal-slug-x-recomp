/* Modo con window (Fase 5): imagen, audio, controles de 2 jugadores, menú, estados guardados.
 * Fase 6C: screen de inicio y menús con la fuente del juego (menu.c, font.c).
 *
 * Teclas fijas: Esc = menú de pausa, P = pausa, F5 / F9 = guardar / cargar estado en la slot active,
 * F11 o Alt+Enter = screen completa, F12 = captura, F2 = botón de test (menú de service del
 * BIOS: dificultad, vidas...), F3 = mode debug (debug.c). En el mando: Guide (o Back + Start)
 * abre el menú.
 *
 * Disparo automático: mientras se mantiene, el botón A se pulsa y se suelta solo, con un periodo
 * de 6, 8 o 12 frames. Medido en partida (command de sonido F7 de la pistola): el juego no
 * dispara más de 10 veces por second aunque el patrón sea más rápido, así que 6 frames es el
 * máximo útil.
 * El resto de los controles se configura en el menú o en mslugx.ini (config.c).
 *
 * Ritmo (Fase 7A): sin V-sync, un cuadro cada 16,896 ms exactos (59,1856 Hz, la placa). Con V-sync:
 *   - monitor de ~60 Hz (58-62): un cuadro por refresco; el juego va a la frecuencia del monitor
 *     (1,4 % más rápido a 60 Hz) y el audio se reproduce en la misma proporción
 *     (SDL_SetAudioStreamFrequencyRatio), con una corrección suave según la cola para no acumular
 *     ni cortar, como el "syncrefresh" de MAME;
 *   - otras frecuencias (75, 120, 144 Hz...): se presenta en cada refresco y se emulan los frames
 *     que tocan según el reloj (acumulador), al ritmo exacto de la placa.
 *
 * Imagen: la Neo Geo dibuja 320x224 píxeles que el monitor estira a 4:3, así que sus píxeles no
 * son cuadrados. El filter "nítido" amplía primero por un factor entero sin suavizar y luego
 * ajusta al tamaño final con interpolación lineal: píxeles parejos sin bordes borrosos.
 */
#include <SDL3/SDL.h>

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "config.h"
#include "debug.h"
#include "dispatcher.h"
#include "frontend.h"
#include "font.h"
#include "logo.h"
#include "menu.h"
#include "neogeo.h"
#include "profile.h"
#include "port.h"
#include "sounds.h"

void machine_start(const Options *o);

Config cfg;
static char dir_pref[1024], ruta_cfg[1100];
static SDL_Window *window;
static SDL_Renderer *render;
static SDL_Texture *tex, *tex_grande;
static int escala_grande;
static SDL_AudioStream *audio;
static SDL_Gamepad *mandos[2];
static int test_cuadros;                     /* frames que falta mantener el botón de test */
static char aviso[256];
static Uint64 aviso_hasta;
static int salir;
static int avanzar;                          /* F6 en el mode debug: un cuadro */
static unsigned auto_fase[2];
static int jugar_cuadro = -1;                /* "Jugar": frames desde que se pidió (-1 = no) */

void fe_notify(const char *fmt, ...)
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
static void estado_ruta(char *r, size_t n) { snprintf(r, n, "%sestado_%d.bin", dir_pref, cfg.slot); }

void fe_state_save(void)
{
    size_t tam;
    uint8_t *d = ng_state_save(&tam);
    char r[1100];
    estado_ruta(r, sizeof r);
    FILE *f = d ? fopen(r, "wb") : NULL;
    int ok = f && fwrite(d, 1, tam, f) == tam;
    if (f && fclose(f) != 0) ok = 0;
    free(d);
    fe_notify(ok ? "State guardado en la slot %d" : "No se pudo guardar la slot %d", cfg.slot);
}

void fe_state_load(void)
{
    char r[1100];
    estado_ruta(r, sizeof r);
    size_t tam;
    void *d = SDL_LoadFile(r, &tam);
    if (!d) { fe_notify("La slot %d esta vacia", cfg.slot); return; }
    if (ng_state_load(d, tam) == 0) fe_notify("State de la slot %d cargado", cfg.slot);
    else fe_notify("La slot %d es de otra version del port", cfg.slot);
    SDL_free(d);
}

/* ------------------------------------------------------------------ inputs */
static void leer_entradas(void)
{
    const bool *k = SDL_GetKeyboardState(NULL);
    for (int j = 0; j < 2; j++) {
        uint8_t joy = 0, sis = 0;
        static const uint8_t BIT_JOY[8] = {NG_UP, NG_DOWN, NG_LEFT, NG_RIGHT, NG_A, NG_B, NG_C, NG_D};
        int pulsado[ACT_COUNT] = {0};
        for (int a = 0; a < ACT_COUNT; a++) {
            if (cfg.key[j][a] != SDL_SCANCODE_UNKNOWN && k[cfg.key[j][a]]) pulsado[a] = 1;
            if (mandos[j] && cfg.button[j][a] != SDL_GAMEPAD_BUTTON_INVALID &&
                SDL_GetGamepadButton(mandos[j], cfg.button[j][a])) pulsado[a] = 1;
        }
        if (mandos[j]) {                       /* la palanca izquierda siempre mueve */
            int ax = SDL_GetGamepadAxis(mandos[j], SDL_GAMEPAD_AXIS_LEFTX);
            int ay = SDL_GetGamepadAxis(mandos[j], SDL_GAMEPAD_AXIS_LEFTY);
            if (ay < -16000) pulsado[ACT_UP] = 1;
            if (ay > 16000) pulsado[ACT_DOWN] = 1;
            if (ax < -16000) pulsado[ACT_LEFT] = 1;
            if (ax > 16000) pulsado[ACT_RIGHT] = 1;
        }
        /* Disparo automático: la mitad del periodo pulsado. */
        int mode = cfg.autofire[j];
        int auto_on = (mode == AUTO_BUTTON && pulsado[ACT_AUTO]) || (mode == AUTO_FIRE && pulsado[ACT_A]);
        if (auto_on) {
            unsigned periodo = (unsigned)AUTO_PERIOD[cfg.auto_speed];
            int pulsar = auto_fase[j]++ % periodo < periodo / 2;
            if (mode == AUTO_FIRE) pulsado[ACT_A] = pulsar;
            else if (pulsar) pulsado[ACT_A] = 1;
        } else {
            auto_fase[j] = 0;
        }
        for (int a = 0; a < 8; a++) if (pulsado[a]) joy |= BIT_JOY[a];
        if (pulsado[ACT_START]) sis |= NG_START;
        if (pulsado[ACT_COIN]) sis |= NG_COIN;
        /* Una dirección y su opuesta a la vez no existen en una palanca real. */
        if ((joy & (NG_UP | NG_DOWN)) == (NG_UP | NG_DOWN)) joy &= ~(NG_UP | NG_DOWN);
        if ((joy & (NG_LEFT | NG_RIGHT)) == (NG_LEFT | NG_RIGHT)) joy &= ~(NG_LEFT | NG_RIGHT);
        ng.joy[j] = joy;
        ng.system[j] = sis;
    }
    if (jugar_cuadro >= 0) {                 /* "Jugar": moneda y, un momento después, START */
        if (jugar_cuadro < 6) ng.system[0] |= NG_COIN;
        else if (jugar_cuadro >= 50 && jugar_cuadro < 56) ng.system[0] |= NG_START;
        if (++jugar_cuadro >= 56) jugar_cuadro = -1;
    }
    ng.test = (k[SDL_SCANCODE_F2] || test_cuadros > 0) ? 0x00 : 0x80;
    if (test_cuadros > 0) test_cuadros--;
}

static void mando_conectado(SDL_JoystickID id)
{
    for (int j = 0; j < 2; j++)
        if (!mandos[j]) {
            mandos[j] = SDL_OpenGamepad(id);
            if (mandos[j]) fe_notify("Mando %d: %s", j + 1, SDL_GetGamepadName(mandos[j]));
            return;
        }
}

static void mando_desconectado(SDL_JoystickID id)
{
    for (int j = 0; j < 2; j++)
        if (mandos[j] && SDL_GetGamepadID(mandos[j]) == id) {
            SDL_CloseGamepad(mandos[j]);
            mandos[j] = NULL;
            fe_notify("Mando %d desconectado", j + 1);
        }
}

/* ------------------------------------------------------------------ acciones de los menús */
void fe_save_config(void) { config_save(&cfg, ruta_cfg); }
static int sin_esperar;                      /* tests: correr lo más rápido posible */

void fe_apply_video(void)
{
    SDL_SetWindowFullscreen(window, cfg.fullscreen != 0);
    SDL_SetRenderVSync(render, cfg.vsync && !sin_esperar ? 1 : 0);
}

/* Frecuencia del monitor de la window (Hz; 60 si no se sabe). */
static float frecuencia_monitor(void)
{
    if (getenv("PD_HZ")) return (float)atof(getenv("PD_HZ"));      /* tests: otra frecuencia */
    const SDL_DisplayMode *m = SDL_GetCurrentDisplayMode(SDL_GetDisplayForWindow(window));
    return m && m->refresh_rate > 1 ? m->refresh_rate : 60.0f;
}
void fe_apply_volume(void) { if (audio) SDL_SetAudioStreamGain(audio, cfg.volume / 100.0f); }
void fe_play(void) { jugar_cuadro = 0; }
void fe_service(void) { test_cuadros = 30; }

/* Sangre roja: el juego lee su opción BLOOD encendida (profile.h). */
void fe_apply_blood(void)
{
    ng_forced_read[NG_FORCED_BLOOD] = (NgForcedRead){cfg.blood, PROFILE_BLOOD_WORD, PROFILE_BLOOD_MASK, 0x0001};
}
void fe_quit(void) { salir = 1; }

void fe_reset(void)
{
    uint8_t sram[sizeof ng.sram];
    memcpy(sram, ng.sram, sizeof sram);
    ng_power_on();
    memcpy(ng.sram, sram, sizeof sram);
    jugar_cuadro = -1;
    fe_notify("Machine reiniciada");
}

/* ------------------------------------------------------------------ dibujo */
static SDL_FRect destino(int w, int h)
{
    double ar = cfg.aspect == ASPECT_4_3 ? 4.0 / 3.0 : (double)NG_WIDTH / NG_HEIGHT;
    double dw = w, dh = w / ar;
    if (dh > h) { dh = h; dw = h * ar; }
    if (cfg.filter == FILTER_PIXEL && cfg.aspect == ASPECT_SQUARE) {
        int s = (int)(dh / NG_HEIGHT);         /* scale entera exacta si cabe */
        if (s >= 1) { dw = NG_WIDTH * s; dh = NG_HEIGHT * s; }
    }
    SDL_FRect r = {(float)((w - dw) / 2), (float)((h - dh) / 2), (float)dw, (float)dh};
    r.x = SDL_roundf(r.x); r.y = SDL_roundf(r.y);
    return r;
}

static SDL_FRect dibujar_juego(int w, int h)
{
    SDL_UpdateTexture(tex, NULL, ng.screen, NG_WIDTH * 4);
    SDL_FRect d = destino(w, h);
    if (cfg.filter == FILTER_SHARP) {
        int s = (int)SDL_ceil(SDL_max(d.w / NG_WIDTH, d.h / NG_HEIGHT));
        s = s < 1 ? 1 : s > 8 ? 8 : s;
        if (s != escala_grande) {
            if (tex_grande) SDL_DestroyTexture(tex_grande);
            tex_grande = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_TARGET,
                                           NG_WIDTH * s, NG_HEIGHT * s);
            SDL_SetTextureScaleMode(tex_grande, SDL_SCALEMODE_LINEAR);
            escala_grande = s;
        }
        SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);
        SDL_SetRenderTarget(render, tex_grande);
        SDL_RenderTexture(render, tex, NULL, NULL);
        SDL_SetRenderTarget(render, NULL);
        SDL_RenderTexture(render, tex_grande, NULL, &d);
    } else {
        SDL_SetTextureScaleMode(tex, cfg.filter == FILTER_SMOOTH ? SDL_SCALEMODE_LINEAR : SDL_SCALEMODE_NEAREST);
        SDL_RenderTexture(render, tex, NULL, &d);
    }
    if (cfg.scanlines && d.h >= NG_HEIGHT * 2) {
        static SDL_FRect lineas[NG_HEIGHT];
        float paso = d.h / NG_HEIGHT;
        for (int y = 0; y < NG_HEIGHT; y++)
            lineas[y] = (SDL_FRect){d.x, d.y + y * paso + paso * 0.55f, d.w, paso * 0.45f};
        SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_BLEND);
        SDL_SetRenderDrawColor(render, 0, 0, 0, 110);
        SDL_RenderFillRects(render, lineas, NG_HEIGHT);
    }
    return d;
}

static void dibujar_avisos(int w, int h)
{
    if (!aviso[0] || SDL_GetTicksNS() >= aviso_hasta) return;
    float k = font_factor(w, h);
    SDL_SetRenderScale(render, k, k);
    SDL_SetRenderDrawBlendMode(render, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(render, 0, 0, 0, 170);
    SDL_FRect f = {4, 4, font_width(aviso, 1) + 8, 14};               /* arriba a la izquierda */
    SDL_RenderFillRect(render, &f);
    font_text(render, 8, 7, 1, INK_SILVER, aviso);
    SDL_SetRenderScale(render, 1, 1);
}

/* ------------------------------------------------------------------ eventos */
/* Navegación común a menús: 0 arriba, 1 abajo, 2 izq., 3 der., 4 elegir, 5 volver, 6 detener,
   7 renombrar, 8 vibración (los dos últimos, solo en el menú de sonido). */
static int nav_tecla(SDL_Scancode sc)
{
    switch (sc) {
    case SDL_SCANCODE_UP: return 0;
    case SDL_SCANCODE_DOWN: return 1;
    case SDL_SCANCODE_LEFT: return 2;
    case SDL_SCANCODE_RIGHT: return 3;
    case SDL_SCANCODE_RETURN: case SDL_SCANCODE_SPACE: case SDL_SCANCODE_KP_ENTER: return 4;
    case SDL_SCANCODE_ESCAPE: return 5;
    case SDL_SCANCODE_DELETE: case SDL_SCANCODE_BACKSPACE: return 6;
    case SDL_SCANCODE_F2: case SDL_SCANCODE_R: return 7;
    case SDL_SCANCODE_V: return 8;
    default: return -1;
    }
}

static int nav_boton(SDL_GamepadButton b)
{
    switch (b) {
    case SDL_GAMEPAD_BUTTON_DPAD_UP: return 0;
    case SDL_GAMEPAD_BUTTON_DPAD_DOWN: return 1;
    case SDL_GAMEPAD_BUTTON_DPAD_LEFT: case SDL_GAMEPAD_BUTTON_LEFT_SHOULDER: return 2;
    case SDL_GAMEPAD_BUTTON_DPAD_RIGHT: case SDL_GAMEPAD_BUTTON_RIGHT_SHOULDER: return 3;
    case SDL_GAMEPAD_BUTTON_SOUTH: return 4;
    case SDL_GAMEPAD_BUTTON_EAST: case SDL_GAMEPAD_BUTTON_GUIDE: return 5;
    case SDL_GAMEPAD_BUTTON_NORTH: return 6;
    case SDL_GAMEPAD_BUTTON_WEST: return 8;
    default: return -1;
    }
}

static void on_key(const SDL_KeyboardEvent *e, int *pausa, int *screenshots)
{
    SDL_Scancode sc = e->scancode;
    if (menu_capturing()) {
        if (!e->repeat) menu_capture_key(sc);
        return;
    }
    if (sounds_editing()) { sounds_edit_key(sc); return; }
    if (sc == SDL_SCANCODE_F11 || (sc == SDL_SCANCODE_RETURN && (e->mod & SDL_KMOD_ALT))) {
        cfg.fullscreen = !cfg.fullscreen;
        fe_apply_video();
        return;
    }
    int nav = nav_tecla(sc);
    if (sounds_open()) { if (nav >= 0) sounds_navigate(nav); return; }
    if (menu_open()) { if (nav >= 0 && nav <= 5) menu_navigate(nav); return; }
    if (e->repeat) return;
    int dbg = debug_key(e);
    if (dbg == DEBUG_STEP) { *pausa = 1; avanzar = 1; }
    if (dbg != DEBUG_NONE) { cfg.debug = debug_enabled(); return; }
    switch (sc) {
    case SDL_SCANCODE_ESCAPE: menu_pause(); break;
    case SDL_SCANCODE_P: *pausa = !*pausa; fe_notify(*pausa ? "Pausa" : "Continuar"); break;
    case SDL_SCANCODE_F5: fe_state_save(); break;
    case SDL_SCANCODE_F9: fe_state_load(); break;
    case SDL_SCANCODE_F12: {
        char r[1100];
        snprintf(r, sizeof r, "%scaptura_%03d.bmp", dir_pref, (*screenshots)++);
        if (save_bmp(r) == 0) fe_notify("Captura: %s", r);
        break;
    }
    default: break;
    }
}

static void on_button(const SDL_GamepadButtonEvent *e)
{
    SDL_GamepadButton b = (SDL_GamepadButton)e->button;
    if (menu_capturing()) { menu_capture_button(b); return; }
    if (sounds_editing()) return;
    SDL_Gamepad *g = SDL_GetGamepadFromID(e->which);
    int combo = g && ((b == SDL_GAMEPAD_BUTTON_START && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_BACK)) ||
                      (b == SDL_GAMEPAD_BUTTON_BACK && SDL_GetGamepadButton(g, SDL_GAMEPAD_BUTTON_START)));
    int nav = nav_boton(b);
    if (sounds_open()) { if (nav >= 0) sounds_navigate(nav); return; }
    if (menu_open()) {
        if (combo) menu_navigate(5);
        else if (nav >= 0 && nav <= 5) menu_navigate(nav);
        return;
    }
    if (b == SDL_GAMEPAD_BUTTON_GUIDE || combo) menu_pause();
}

/* ------------------------------------------------------------------ ROMs */
static volatile int dialogo_listo;
static char dialogo_ruta[1024];

static void SDLCALL al_elegir_carpeta(void *data, const char *const *lista, int filter)
{
    (void)data; (void)filter;
    if (lista && lista[0]) snprintf(dialogo_ruta, sizeof dialogo_ruta, "%s", lista[0]);
    dialogo_listo = 1;
}

static int probar_roms(const char *carpeta, char *error, int tam)
{
    if (ng_roms_load(&ng_roms, carpeta, error, tam) == 0) {
        snprintf(cfg.roms, sizeof cfg.roms, "%s", carpeta);
        return 1;
    }
    char sub[1100];                          /* también vale la carpeta raíz del juego */
    snprintf(sub, sizeof sub, "%s\\resources\\game", carpeta);
    if (ng_roms_load(&ng_roms, sub, error, tam) == 0) {
        snprintf(cfg.roms, sizeof cfg.roms, "%s", sub);
        return 1;
    }
    return 0;
}

/* --roms, la carpeta recordada, "roms" junto al ejecutable, la instalación de GOG y, si nada
   sirve, la carpeta que elija el usuario. */
static int buscar_roms(const Options *o)
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
    if (gog_find(carpeta, sizeof carpeta) && probar_roms(carpeta, error, sizeof error)) return 1;
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
int window_run(const Options *o)
{
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | (o->no_audio ? 0 : SDL_INIT_AUDIO))) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    /* PD_PREF: otra carpeta de preferencias (tests, sin tocar la configuración del usuario). */
    char *pref = getenv("PD_PREF") ? NULL : SDL_GetPrefPath("ProjectoDecompile", "mslugx");
    snprintf(dir_pref, sizeof dir_pref, "%s", pref ? pref : getenv("PD_PREF") ? getenv("PD_PREF") : "");
    SDL_free(pref);
    snprintf(ruta_cfg, sizeof ruta_cfg, "%smslugx.ini", dir_pref);
    config_defaults(&cfg);
    config_load(&cfg, ruta_cfg);
    if (o->scale > 0) cfg.scale = o->scale;
    if (o->turbo >= 0) cfg.turbo = o->turbo;

    if (!buscar_roms(o)) { SDL_Quit(); return 1; }
    config_save(&cfg, ruta_cfg);

    ng_audio_enabled = !o->no_audio;
    cpu_turbo = cfg.turbo;
    machine_start(o);
    debug_enable(cfg.debug);
    fe_apply_blood();
    rumble_init(mandos, 2);
    rumble_strength(cfg.rumble);
    if (!o->empty_sram) sram_cargar();

    int alto = NG_HEIGHT * cfg.scale;
    int ancho = cfg.aspect == ASPECT_4_3 ? alto * 4 / 3 : NG_WIDTH * cfg.scale;
    if (!SDL_CreateWindowAndRenderer("Metal Slug X", ancho, alto, SDL_WINDOW_RESIZABLE, &window, &render)) {
        fprintf(stderr, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetWindowMinimumSize(window, NG_WIDTH, NG_HEIGHT);
    tex = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, NG_WIDTH, NG_HEIGHT);
    fe_apply_video();
    font_create(render);
    {
        char r[1100];
        snprintf(r, sizeof r, "%slogo_titulo.png", dir_pref);
        logo_prepare(render, r);              /* la primera vez corre la máquina hasta el título */
    }
    {
        char r[1100];
        snprintf(r, sizeof r, "%ssonidos.ini", dir_pref);
        sounds_init(window, r);
    }
    if (cfg.start_screen) menu_start();

    /* Audio: SDL convierte de 55 555 Hz a la frecuencia del dispositivo. */
    if (ng_audio_enabled) {
        const SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, NG_AUDIO_HZ};
        audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (audio) {
            /* Colchón inicial de 50 ms de silencio contra las variaciones del ritmo del video. */
            static const int16_t silencio[NG_AUDIO_HZ / 20 * 2];
            SDL_PutAudioStreamData(audio, silencio, (int)sizeof silencio);
            SDL_SetAudioStreamGain(audio, cfg.volume / 100.0f);
            SDL_ResumeAudioStreamDevice(audio);
        }
        else { fprintf(stderr, "Sin audio: %s\n", SDL_GetError()); ng_audio_enabled = 0; }
    }
    static int16_t muestras[4096 * 2];
    uint64_t audio_cuadros = 0, audio_descartes = 0, audio_vacios = 0, audio_cola_suma = 0;
    if (!cfg.start_screen) fe_notify("Esc: menu   F5/F9: guardar/cargar partida");

    /* PD_PRUEBA_VENTANA=N,archivo.png[,menu|sonidos|jugar]: tras N frames guarda lo que muestra la
       window (con el menú, con el menú de sonido reproduciendo la segunda música, o tras elegir
       "Jugar" en el cuadro 1300) y sale. */
    long prueba_cuadros = 0;
    char prueba_png[1024] = "";
    int prueba_menu = 0, prueba_sonidos = 0, prueba_jugar = 0, prueba_renombrar = 0;
    if (getenv("PD_PRUEBA_VENTANA")) {
        char extra[16] = "";
        sscanf(getenv("PD_PRUEBA_VENTANA"), "%ld,%1023[^,],%15s", &prueba_cuadros, prueba_png, extra);
        prueba_menu = !strcmp(extra, "menu");
        prueba_sonidos = !strcmp(extra, "sonidos");
        prueba_jugar = !strcmp(extra, "jugar");
        prueba_renombrar = !strcmp(extra, "renombrar");
    }
    long cuadros_vistos = 0;
    if (prueba_cuadros) { sin_esperar = 1; fe_apply_video(); }

    int pausa = 0, screenshots = 0;
    Uint64 proximo = SDL_GetTicksNS(), anterior = proximo, acumulado = 0, inicio = proximo;
    float hz = frecuencia_monitor();
    /* PD_SALIR_TRAS=segundos: sale sola (medición del ritmo en tiempo real, con --traza). */
    double salir_tras = getenv("PD_SALIR_TRAS") ? atof(getenv("PD_SALIR_TRAS")) : 0;
    uint64_t vueltas = 0, emulados = ng.frames;
    while (!salir) {
        if (prueba_cuadros && prueba_menu && cuadros_vistos == prueba_cuadros - 1) menu_pause();
        if (prueba_cuadros && prueba_jugar && cuadros_vistos == 1300) menu_navigate(4);   /* "Jugar" */
        if (prueba_cuadros && prueba_renombrar) {           /* renombrar dos veces el mismo efecto */
            long f = prueba_cuadros - cuadros_vistos;
            if (f == 400) { sounds_show(); sounds_navigate(3); sounds_navigate(7); }
            if (f == 300 || f == 100) {
                SDL_Delay(80);                              /* pasa la window que ignora la key R */
                if (f == 100) for (int i = 0; i < 40; i++) sounds_edit_key(SDL_SCANCODE_BACKSPACE);
                sounds_text(f == 300 ? "Test uno" : "Test dos");
                sounds_edit_key(SDL_SCANCODE_RETURN);
            }
            if (f == 200) sounds_navigate(7);
        }
        if (prueba_cuadros && prueba_sonidos && cuadros_vistos == prueba_cuadros - 60) {
            sounds_show();
            sounds_navigate(1);
            sounds_navigate(4);
        }
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT: salir = 1; break;
            case SDL_EVENT_KEY_DOWN: on_key(&e.key, &pausa, &screenshots); break;
            case SDL_EVENT_TEXT_INPUT: sounds_text(e.text.text); break;
            case SDL_EVENT_GAMEPAD_BUTTON_DOWN: on_button(&e.gbutton); break;
            case SDL_EVENT_GAMEPAD_ADDED: mando_conectado(e.gdevice.which); break;
            case SDL_EVENT_GAMEPAD_REMOVED: mando_desconectado(e.gdevice.which); break;
            case SDL_EVENT_WINDOW_ENTER_FULLSCREEN: cfg.fullscreen = 1; hz = frecuencia_monitor(); break;
            case SDL_EVENT_WINDOW_LEAVE_FULLSCREEN: cfg.fullscreen = 0; hz = frecuencia_monitor(); break;
            case SDL_EVENT_WINDOW_DISPLAY_CHANGED: hz = frecuencia_monitor(); break;
            default: break;
            }
        }
        /* Cuántos frames emular en esta vuelta (ver el comentario del principio). */
        Uint64 ahora = SDL_GetTicksNS();
        int vsync = cfg.vsync && !sin_esperar;
        int exacto = vsync && hz >= 58.0f && hz <= 62.0f;
        int frames = 1;
        if (vsync && !exacto) {
            acumulado += ahora - anterior;
            if (acumulado > 4 * NS_PER_FRAME) acumulado = NS_PER_FRAME;      /* nos atrasamos */
            frames = (int)(acumulado / NS_PER_FRAME);
            acumulado -= (Uint64)frames * NS_PER_FRAME;
        }
        anterior = ahora;
        vueltas++;
        if (salir_tras > 0 && (ahora - inicio) / 1e9 >= salir_tras) salir = 1;
        for (int c = 0; c < frames; c++) {
            if (sounds_open()) {
                sounds_frame();
            } else if (menu_game_runs()) {     /* screen de inicio: el demo sigue, sin inputs */
                memset(ng.joy, 0, sizeof ng.joy);
                memset(ng.system, 0, sizeof ng.system);
                ng.test = 0x80;
                ng_run_frame();
            } else if ((!pausa || avanzar) && !menu_open()) {
                leer_entradas();
                ng_run_frame();
                debug_frame();
                rumble_frame();
                avanzar = 0;
            }
        }
        if (audio && vsync) {
            /* Con V-sync el reloj es el monitor: el audio se reproduce en la proporción del ritmo
               de los frames, con una corrección de hasta ±0,5 % para mantener la cola en ~50 ms. */
            float base = exacto ? hz / 59.1856f : 1.0f;
            float objetivo = NG_AUDIO_HZ * 4 / 20.0f;
            float error = ((float)SDL_GetAudioStreamQueued(audio) - objetivo) / objetivo;
            if (error > 1) error = 1;
            if (error < -1) error = -1;
            SDL_SetAudioStreamFrequencyRatio(audio, base * (1.0f + 0.005f * error));
        } else if (audio) {
            SDL_SetAudioStreamFrequencyRatio(audio, 1.0f);
        }
        if (audio) {
            uint32_t n = ng_audio_read(muestras, 4096);
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
        debug_draw(render, juego, w, h, cfg.turbo);
        if (sounds_open()) sounds_draw(render, w, h);
        else menu_draw(render, w, h);
        dibujar_avisos(w, h);
        if (prueba_cuadros && ++cuadros_vistos >= prueba_cuadros) {
            SDL_Surface *s = SDL_RenderReadPixels(render, NULL);
            if (s) { SDL_SavePNG(s, prueba_png); SDL_DestroySurface(s); }
            salir = 1;
        }
        SDL_RenderPresent(render);

        /* Sin V-sync, el ritmo lo marca la espera: un cuadro cada 16,896 ms. */
        proximo += NS_PER_FRAME;
        ahora = SDL_GetTicksNS();
        if (sin_esperar || vsync) proximo = ahora;          /* tests, o el V-sync ya esperó */
        else if (proximo > ahora) SDL_DelayPrecise(proximo - ahora);
        else if (ahora - proximo > 4 * NS_PER_FRAME) proximo = ahora;   /* nos atrasamos: no acumular */
    }

    sounds_close();                    /* la SRAM y el estado vuelven a ser los del juego */
    if (!o->empty_sram) sram_guardar();
    config_save(&cfg, ruta_cfg);
    if (ng_trace) {
        double s = (SDL_GetTicksNS() - inicio) / 1e9;
        fprintf(ng_trace, "ritmo: monitor %.2f Hz, vsync %d, %.2f vueltas/s, %.3f frames emulados/s\n", hz, cfg.vsync,
                vueltas / s, (ng.frames - emulados) / s);
    }
    if (audio && ng_trace && audio_cuadros)
        fprintf(ng_trace, "audio: %llu frames, cola media %.1f ms, cola vacía %llu veces, descartes %llu\n",
                (unsigned long long)audio_cuadros, audio_cola_suma / (double)audio_cuadros / (NG_AUDIO_HZ * 4) * 1000,
                (unsigned long long)audio_vacios, (unsigned long long)audio_descartes);
    if (audio) SDL_DestroyAudioStream(audio);
    for (int j = 0; j < 2; j++) if (mandos[j]) SDL_CloseGamepad(mandos[j]);
    font_destroy();
    logo_destroy();
    if (tex_grande) SDL_DestroyTexture(tex_grande);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(render);
    SDL_DestroyWindow(window);
    SDL_Quit();
    ng_roms_free(&ng_roms);
    return 0;
}
