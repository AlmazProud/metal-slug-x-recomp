/* Metal Slug X, port nativo: ventana, controles y ritmo con SDL3.
 *
 * Uso:
 *   mslugx [--roms CARPETA] [--escala N] [--sram-vacia] [--fecha AAAA-MM-DD-hh-mm-ss-D]
 *   mslugx --sin-ventana --cuadros N [--capturas a,b,c --salida CARPETA] ...
 *
 * --roms: carpeta resources\game de la instalación de GOG (por defecto, "roms" junto al
 *         ejecutable). Los archivos se validan por SHA-256.
 * --sin-ventana: corre N cuadros lo más rápido posible y guarda capturas BMP de los cuadros
 *         pedidos (para compararlas contra MAME con herramientas/comparar_capturas.py).
 * --wav: con --sin-ventana, guarda el audio generado (estéreo, 16 bits, 55 555 Hz).
 * --sin-audio: con ventana, no abre el dispositivo de audio.
 * --guion: entradas para --sin-ventana, una línea por tramo: "desde hasta entrada..." (cuadros
 *         inclusive; entradas: arriba abajo izquierda derecha a b c d start1 start2 moneda1
 *         moneda2 test). Las líneas que empiezan con # se ignoran.
 *
 * Teclado: flechas, Z = A (disparo), X = B (salto), C = C (granada), V = D, 1/2 = START,
 * 5/6 = moneda, F2 = botón de test, F12 = captura, P = pausa, Esc = salir.
 * Mando: cruceta o palanca izquierda, X/A/B/Y = A/B/C/D, Start = START, Back = moneda.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "despachador.h"
#include "neogeo.h"

void programa_registrar(void);

#define NS_POR_CUADRO 16896000LL         /* 202 752 ciclos a 12 MHz: 59,1856 Hz */

typedef struct {
    const char *roms;
    int escala, sin_ventana, sram_vacia;
    long cuadros;
    const char *capturas, *salida, *guion, *wav;
    int sin_audio;
    int fecha[7], con_fecha;
} Opciones;

/* ------------------------------------------------------------------ guion de entradas */
typedef struct { unsigned long long desde, hasta; uint8_t joy, sis[2], test; } Tramo;
static Tramo *tramos;
static size_t n_tramos;

static int guion_cargar(const char *ruta)
{
    FILE *f = fopen(ruta, "r");
    if (!f) { fprintf(stderr, "No se pudo abrir el guion %s\n", ruta); return -1; }
    char linea[512];
    while (fgets(linea, sizeof linea, f)) {
        if (linea[0] == '#') continue;
        Tramo tr = {0};
        int usados = 0;
        if (sscanf(linea, "%llu %llu %n", &tr.desde, &tr.hasta, &usados) < 2) continue;
        for (char *tok = strtok(linea + usados, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
            if (!strcmp(tok, "arriba")) tr.joy |= NG_ARRIBA;
            else if (!strcmp(tok, "abajo")) tr.joy |= NG_ABAJO;
            else if (!strcmp(tok, "izquierda")) tr.joy |= NG_IZQUIERDA;
            else if (!strcmp(tok, "derecha")) tr.joy |= NG_DERECHA;
            else if (!strcmp(tok, "a")) tr.joy |= NG_A;
            else if (!strcmp(tok, "b")) tr.joy |= NG_B;
            else if (!strcmp(tok, "c")) tr.joy |= NG_C;
            else if (!strcmp(tok, "d")) tr.joy |= NG_D;
            else if (!strcmp(tok, "start1")) tr.sis[0] |= NG_START;
            else if (!strcmp(tok, "start2")) tr.sis[1] |= NG_START;
            else if (!strcmp(tok, "moneda1")) tr.sis[0] |= NG_MONEDA;
            else if (!strcmp(tok, "moneda2")) tr.sis[1] |= NG_MONEDA;
            else if (!strcmp(tok, "test")) tr.test = 1;
            else { fprintf(stderr, "Entrada desconocida en el guion: %s\n", tok); fclose(f); return -1; }
        }
        tramos = realloc(tramos, (n_tramos + 1) * sizeof *tramos);
        tramos[n_tramos++] = tr;
    }
    fclose(f);
    return 0;
}

/* Entradas del cuadro que está por ejecutarse: ng.cuadros, con la numeración de MAME. */
static void guion_aplicar(void)
{
    uint8_t joy = 0, s0 = 0, s1 = 0, test = 0;
    for (size_t i = 0; i < n_tramos; i++)
        if (ng.cuadros >= tramos[i].desde && ng.cuadros <= tramos[i].hasta) {
            joy |= tramos[i].joy; s0 |= tramos[i].sis[0]; s1 |= tramos[i].sis[1]; test |= tramos[i].test;
        }
    ng.joy[0] = joy;
    ng.sistema[0] = s0;
    ng.sistema[1] = s1;
    ng.test = test ? 0x00 : 0x80;
}

/* ------------------------------------------------------------------ capturas BMP */
static int guardar_bmp(const char *ruta)
{
    FILE *f = fopen(ruta, "wb");
    if (!f) return -1;
    const uint32_t fila = NG_ANCHO * 3, datos = fila * NG_ALTO, total = 54 + datos;
    uint8_t h[54] = {'B', 'M'};
    #define P32(o, v) do { h[o] = (uint8_t)(v); h[o + 1] = (uint8_t)((v) >> 8); h[o + 2] = (uint8_t)((v) >> 16); h[o + 3] = (uint8_t)((v) >> 24); } while (0)
    P32(2, total); P32(10, 54); P32(14, 40); P32(18, NG_ANCHO); P32(22, NG_ALTO);
    h[26] = 1; h[28] = 24; P32(34, datos);
    #undef P32
    fwrite(h, 1, 54, f);
    uint8_t linea[NG_ANCHO * 3];
    for (int y = NG_ALTO - 1; y >= 0; y--) {
        for (int x = 0; x < NG_ANCHO; x++) {
            uint32_t p = ng.pantalla[y][x];
            linea[3 * x] = (uint8_t)p; linea[3 * x + 1] = (uint8_t)(p >> 8); linea[3 * x + 2] = (uint8_t)(p >> 16);
        }
        fwrite(linea, 1, sizeof linea, f);
    }
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ WAV */
static FILE *wav;
static uint32_t wav_muestras;

static void wav_cabecera(FILE *f, uint32_t muestras)
{
    uint32_t datos = muestras * 4, hz = NG_AUDIO_HZ, bps = hz * 4;
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
                     16, 0, 0, 0, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 16, 0,
                     'd', 'a', 't', 'a', 0, 0, 0, 0};
    #define P32(o, v) do { h[o] = (uint8_t)(v); h[o + 1] = (uint8_t)((v) >> 8); h[o + 2] = (uint8_t)((v) >> 16); h[o + 3] = (uint8_t)((v) >> 24); } while (0)
    P32(4, 36 + datos); P32(24, hz); P32(28, bps); P32(40, datos);
    #undef P32
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, f);
    fseek(f, 0, SEEK_END);
}

static void wav_volcar(void)
{
    int16_t b[4096 * 2];
    uint32_t n;
    while ((n = ng_audio_leer(b, 4096)) > 0) {
        fwrite(b, 4, n, wav);
        wav_muestras += n;
    }
}

/* ------------------------------------------------------------------ SRAM */
static char ruta_sram[1024];

static void sram_cargar(void)
{
    char *pref = SDL_GetPrefPath("ProjectoDecompile", "mslugx");
    if (!pref) return;
    snprintf(ruta_sram, sizeof ruta_sram, "%ssram.bin", pref);
    SDL_free(pref);
    FILE *f = fopen(ruta_sram, "rb");
    if (f) {
        if (fread(ng.sram, 1, sizeof ng.sram, f) != sizeof ng.sram) memset(ng.sram, 0, sizeof ng.sram);
        fclose(f);
    }
}

static void sram_guardar(void)
{
    if (!ruta_sram[0]) return;
    FILE *f = fopen(ruta_sram, "wb");
    if (f) { fwrite(ng.sram, 1, sizeof ng.sram, f); fclose(f); }
}

/* ------------------------------------------------------------------ entradas */
static SDL_Gamepad *mando;

static void leer_entradas(void)
{
    const bool *k = SDL_GetKeyboardState(NULL);
    uint8_t j = 0, s = 0, s2 = 0;
    if (k[SDL_SCANCODE_UP]) j |= NG_ARRIBA;
    if (k[SDL_SCANCODE_DOWN]) j |= NG_ABAJO;
    if (k[SDL_SCANCODE_LEFT]) j |= NG_IZQUIERDA;
    if (k[SDL_SCANCODE_RIGHT]) j |= NG_DERECHA;
    if (k[SDL_SCANCODE_Z]) j |= NG_A;
    if (k[SDL_SCANCODE_X]) j |= NG_B;
    if (k[SDL_SCANCODE_C]) j |= NG_C;
    if (k[SDL_SCANCODE_V]) j |= NG_D;
    if (k[SDL_SCANCODE_1]) s |= NG_START;
    if (k[SDL_SCANCODE_5]) s |= NG_MONEDA;
    if (k[SDL_SCANCODE_2]) s2 |= NG_START;
    if (k[SDL_SCANCODE_6]) s2 |= NG_MONEDA;
    if (mando) {
        if (SDL_GetGamepadButton(mando, SDL_GAMEPAD_BUTTON_DPAD_UP)) j |= NG_ARRIBA;
        if (SDL_GetGamepadButton(mando, SDL_GAMEPAD_BUTTON_DPAD_DOWN)) j |= NG_ABAJO;
        if (SDL_GetGamepadButton(mando, SDL_GAMEPAD_BUTTON_DPAD_LEFT)) j |= NG_IZQUIERDA;
        if (SDL_GetGamepadButton(mando, SDL_GAMEPAD_BUTTON_DPAD_RIGHT)) j |= NG_DERECHA;
        int ax = SDL_GetGamepadAxis(mando, SDL_GAMEPAD_AXIS_LEFTX), ay = SDL_GetGamepadAxis(mando, SDL_GAMEPAD_AXIS_LEFTY);
        if (ay < -16000) j |= NG_ARRIBA;
        if (ay > 16000) j |= NG_ABAJO;
        if (ax < -16000) j |= NG_IZQUIERDA;
        if (ax > 16000) j |= NG_DERECHA;
        if (SDL_GetGamepadButton(mando, SDL_GAMEPAD_BUTTON_WEST)) j |= NG_A;
        if (SDL_GetGamepadButton(mando, SDL_GAMEPAD_BUTTON_SOUTH)) j |= NG_B;
        if (SDL_GetGamepadButton(mando, SDL_GAMEPAD_BUTTON_EAST)) j |= NG_C;
        if (SDL_GetGamepadButton(mando, SDL_GAMEPAD_BUTTON_NORTH)) j |= NG_D;
        if (SDL_GetGamepadButton(mando, SDL_GAMEPAD_BUTTON_START)) s |= NG_START;
        if (SDL_GetGamepadButton(mando, SDL_GAMEPAD_BUTTON_BACK)) s |= NG_MONEDA;
    }
    /* Una dirección y su opuesta a la vez no existen en una palanca real. */
    if ((j & (NG_ARRIBA | NG_ABAJO)) == (NG_ARRIBA | NG_ABAJO)) j &= ~(NG_ARRIBA | NG_ABAJO);
    if ((j & (NG_IZQUIERDA | NG_DERECHA)) == (NG_IZQUIERDA | NG_DERECHA)) j &= ~(NG_IZQUIERDA | NG_DERECHA);
    ng.joy[0] = j;
    ng.sistema[0] = s;
    ng.sistema[1] = s2;
    ng.test = k[SDL_SCANCODE_F2] ? 0x00 : 0x80;
}

/* ------------------------------------------------------------------ opciones */
static void uso(void)
{
    fprintf(stderr,
            "Uso: mslugx [--roms CARPETA] [--escala N] [--sram-vacia] [--fecha AAAA-MM-DD-hh-mm-ss-D]\n"
            "            [--sin-ventana --cuadros N [--capturas a,b,c --salida CARPETA] [--guion ARCHIVO] [--wav ARCHIVO]]\n"
            "            [--sin-audio] [--traza ARCHIVO] [--traza-es ARCHIVO DESDE_US HASTA_US]\n");
}

static int opciones(int argc, char **argv, Opciones *o)
{
    memset(o, 0, sizeof *o);
    o->escala = 3;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--roms") && v) { o->roms = v; i++; }
        else if (!strcmp(a, "--escala") && v) { o->escala = atoi(v); i++; }
        else if (!strcmp(a, "--cuadros") && v) { o->cuadros = atol(v); i++; }
        else if (!strcmp(a, "--capturas") && v) { o->capturas = v; i++; }
        else if (!strcmp(a, "--salida") && v) { o->salida = v; i++; }
        else if (!strcmp(a, "--guion") && v) { o->guion = v; i++; }
        else if (!strcmp(a, "--wav") && v) { o->wav = v; i++; }
        else if (!strcmp(a, "--sin-audio")) o->sin_audio = 1;
        else if (!strcmp(a, "--traza") && v) { ng_traza = fopen(v, "w"); i++; }
        else if (!strcmp(a, "--traza-es") && i + 3 < argc) {
            /* --traza-es ARCHIVO DESDE_US HASTA_US */
            ng_traza_es = fopen(argv[i + 1], "w");
            ng_traza_desde = (int64_t)(atof(argv[i + 2]) * 12.0);
            ng_traza_hasta = (int64_t)(atof(argv[i + 3]) * 12.0);
            i += 3;
        }
        else if (!strcmp(a, "--sin-ventana")) o->sin_ventana = 1;
        else if (!strcmp(a, "--sram-vacia")) o->sram_vacia = 1;
        else if (!strcmp(a, "--fecha") && v) {
            int *f = o->fecha;
            if (sscanf(v, "%d-%d-%d-%d-%d-%d-%d", &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6]) != 7) return -1;
            o->con_fecha = 1;
            i++;
        } else return -1;
    }
    if (o->escala < 1) o->escala = 1;
    if (o->sin_ventana && o->cuadros <= 0) return -1;
    return 0;
}

static int pedida(const char *lista, unsigned long long cuadro)
{
    if (!lista) return 0;
    for (const char *p = lista; *p;) {
        char *fin;
        unsigned long long v = strtoull(p, &fin, 10);
        if (fin == p) return 0;
        if (v == cuadro) return 1;
        p = *fin == ',' ? fin + 1 : fin;
    }
    return 0;
}

/* ------------------------------------------------------------------ principal */
int main(int argc, char **argv)
{
    Opciones o;
    if (opciones(argc, argv, &o) != 0) { uso(); return 2; }

    char carpeta[1024];
    if (o.roms) snprintf(carpeta, sizeof carpeta, "%s", o.roms);
    else {
        const char *base = SDL_GetBasePath();
        snprintf(carpeta, sizeof carpeta, "%sroms", base ? base : "");
    }
    char error[1200];
    if (ng_roms_cargar(&ng_roms, carpeta, error, sizeof error) != 0) {
        fprintf(stderr, "%s\n", error);
        fprintf(stderr, "Indica la carpeta resources\\game de tu instalación de GOG con --roms.\n");
        if (!o.sin_ventana)
            SDL_ShowSimpleMessageBox(SDL_MESSAGEBOX_ERROR, "Metal Slug X", error, NULL);
        return 1;
    }

    programa_registrar();
    rc_interprete_iniciar();
    if (o.sin_ventana) {
        if (o.wav) {
            wav = fopen(o.wav, "wb");
            if (!wav) { fprintf(stderr, "No se pudo crear %s\n", o.wav); return 1; }
            wav_cabecera(wav, 0);
            ng_audio_activo = 1;
        }
    } else {
        ng_audio_activo = !o.sin_audio;
    }
    if (o.con_fecha) ng_rtc_fijar(o.fecha[0], o.fecha[1], o.fecha[2], o.fecha[6], o.fecha[3], o.fecha[4], o.fecha[5]);
    ng_encender();

    /* ---------------------------------------------- sin ventana: corrida determinista */
    if (o.sin_ventana) {
        if (o.guion && guion_cargar(o.guion) != 0) return 2;
        Uint64 t0 = SDL_GetTicksNS();
        for (long n = 0; n < o.cuadros; n++) {
            guion_aplicar();
            ng_ejecutar_cuadro();
            if (wav) wav_volcar();
            /* Numeración de MAME (screen:frame_number()): la imagen que se completa al ejecutar
               el cuadro N (ng.cuadros pasa de N a N + 1) es el cuadro N. Verificado con
               capturas: 447 de 450 cuadros consecutivos idénticos con esta numeración. */
            unsigned long long etiqueta = ng.cuadros - 1;
            if (pedida(o.capturas, etiqueta)) {
                char ruta[1200];
                snprintf(ruta, sizeof ruta, "%s/port_%06llu.bmp", o.salida ? o.salida : ".", etiqueta);
                if (guardar_bmp(ruta) != 0) fprintf(stderr, "No se pudo escribir %s\n", ruta);
            }
        }
        double s = (double)(SDL_GetTicksNS() - t0) / 1e9;
        printf("cuadros: %llu en %.2f s (%.0f cuadros/s)  pasos recompilados: %llu  interpretados: %llu\n",
               (unsigned long long)ng.cuadros, s, ng.cuadros / (s > 0 ? s : 1),
               (unsigned long long)rc_pasos_recompilados, (unsigned long long)rc_pasos_interpretados);
        printf("lecturas sin mapear: %llu  escrituras sin mapear: %llu  reinicios del watchdog: %u\n",
               (unsigned long long)ng.lecturas_sin_mapear, (unsigned long long)ng.escrituras_sin_mapear,
               ng.reinicios_watchdog);
        if (wav) { wav_cabecera(wav, wav_muestras); fclose(wav); }
        ng_roms_liberar(&ng_roms);
        return 0;
    }

    /* ---------------------------------------------- con ventana */
    if (!SDL_Init(SDL_INIT_VIDEO | SDL_INIT_GAMEPAD | (ng_audio_activo ? SDL_INIT_AUDIO : 0))) {
        fprintf(stderr, "SDL_Init: %s\n", SDL_GetError());
        return 1;
    }
    if (!o.sram_vacia) sram_cargar();

    SDL_Window *ventana = NULL;
    SDL_Renderer *render = NULL;
    if (!SDL_CreateWindowAndRenderer("Metal Slug X", NG_ANCHO * o.escala, NG_ALTO * o.escala,
                                     SDL_WINDOW_RESIZABLE, &ventana, &render)) {
        fprintf(stderr, "SDL_CreateWindowAndRenderer: %s\n", SDL_GetError());
        return 1;
    }
    SDL_SetRenderLogicalPresentation(render, NG_ANCHO, NG_ALTO, SDL_LOGICAL_PRESENTATION_INTEGER_SCALE);
    SDL_Texture *tex = SDL_CreateTexture(render, SDL_PIXELFORMAT_ARGB8888, SDL_TEXTUREACCESS_STREAMING, NG_ANCHO, NG_ALTO);
    SDL_SetTextureScaleMode(tex, SDL_SCALEMODE_NEAREST);

    /* Audio: SDL convierte de 55 555 Hz a la frecuencia del dispositivo. */
    SDL_AudioStream *audio = NULL;
    if (ng_audio_activo) {
        const SDL_AudioSpec spec = {SDL_AUDIO_S16, 2, NG_AUDIO_HZ};
        audio = SDL_OpenAudioDeviceStream(SDL_AUDIO_DEVICE_DEFAULT_PLAYBACK, &spec, NULL, NULL);
        if (audio) {
            /* Colchón inicial de 50 ms de silencio contra las variaciones del ritmo del video. */
            static const int16_t silencio[NG_AUDIO_HZ / 20 * 2];
            SDL_PutAudioStreamData(audio, silencio, (int)sizeof silencio);
            SDL_ResumeAudioStreamDevice(audio);
        }
        else { fprintf(stderr, "Sin audio: %s\n", SDL_GetError()); ng_audio_activo = 0; }
    }
    static int16_t muestras[4096 * 2];
    uint64_t audio_cuadros = 0, audio_descartes = 0, audio_vacios = 0, audio_cola_suma = 0;

    int salir = 0, pausa = 0, capturas = 0;
    Uint64 proximo = SDL_GetTicksNS();
    while (!salir) {
        SDL_Event e;
        while (SDL_PollEvent(&e)) {
            switch (e.type) {
            case SDL_EVENT_QUIT: salir = 1; break;
            case SDL_EVENT_KEY_DOWN:
                if (e.key.repeat) break;
                if (e.key.scancode == SDL_SCANCODE_ESCAPE) salir = 1;
                else if (e.key.scancode == SDL_SCANCODE_P) pausa = !pausa;
                else if (e.key.scancode == SDL_SCANCODE_F12) {
                    char ruta[64];
                    snprintf(ruta, sizeof ruta, "captura_%03d.bmp", capturas++);
                    guardar_bmp(ruta);
                }
                break;
            case SDL_EVENT_GAMEPAD_ADDED:
                if (!mando) mando = SDL_OpenGamepad(e.gdevice.which);
                break;
            case SDL_EVENT_GAMEPAD_REMOVED:
                if (mando && SDL_GetGamepadID(mando) == e.gdevice.which) { SDL_CloseGamepad(mando); mando = NULL; }
                break;
            default: break;
            }
        }
        if (!pausa) {
            leer_entradas();
            ng_ejecutar_cuadro();
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
        SDL_UpdateTexture(tex, NULL, ng.pantalla, NG_ANCHO * 4);
        SDL_SetRenderDrawColor(render, 0, 0, 0, 255);
        SDL_RenderClear(render);
        SDL_RenderTexture(render, tex, NULL, NULL);
        SDL_RenderPresent(render);

        proximo += NS_POR_CUADRO;
        Uint64 ahora = SDL_GetTicksNS();
        if (proximo > ahora) SDL_DelayPrecise(proximo - ahora);
        else if (ahora - proximo > 4 * NS_POR_CUADRO) proximo = ahora;   /* nos atrasamos: no acumular */
    }

    if (!o.sram_vacia) sram_guardar();
    if (audio && ng_traza && audio_cuadros)
        fprintf(ng_traza, "audio: %llu cuadros, cola media %.1f ms, cola vacía %llu veces, descartes %llu\n",
                (unsigned long long)audio_cuadros, audio_cola_suma / (double)audio_cuadros / (NG_AUDIO_HZ * 4) * 1000,
                (unsigned long long)audio_vacios, (unsigned long long)audio_descartes);
    if (audio) SDL_DestroyAudioStream(audio);
    if (mando) SDL_CloseGamepad(mando);
    SDL_DestroyTexture(tex);
    SDL_DestroyRenderer(render);
    SDL_DestroyWindow(ventana);
    SDL_Quit();
    ng_roms_liberar(&ng_roms);
    return 0;
}
