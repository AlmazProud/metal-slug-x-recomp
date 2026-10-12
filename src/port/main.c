/* Metal Slug X, port nativo: opciones de línea de comandos y arranque.
 *
 * Uso:
 *   mslugx [--roms CARPETA] [--scale N] [--turbo N] [--sram-vacia] [--sin-audio]
 *          [--date AAAA-MM-DD-hh-mm-ss-D]
 *   mslugx --sin-window --frames N [--screenshots a,b,c --output CARPETA] ...
 *
 * --roms: carpeta resources\game de la instalación de GOG. Sin esta opción, el mode con window
 *         la busca solo (window.c); el mode sin window usa "roms" junto al ejecutable. Los
 *         archivos se validan por SHA-256.
 * --turbo: 0 = como el original; 1 / 2 = 68000 al doble / cuádruple (sin la ralentización
 *         original). Con window, por defecto se usa el de mslugx.ini.
 * --sin-window: corre N frames lo más rápido posible (verificación contra MAME, headless.c):
 *   --screenshots: guarda BMP de los frames pedidos (tools/compare_screenshots.py).
 *   --wav: guarda el audio generado (estéreo, 16 bits, 55 555 Hz).
 *   --inputs: inputs grabadas en MAME (tools/mame/inputs.lua, líneas "C" con tiempo,
 *         o "E cuadro P1 P2 SIS MON TEST"). --fingerprints: fingerprints por cuadro y por VBlank (Fase 4).
 *   --script: inputs por tramos: "desde hasta entrada..." (frames inclusive; inputs: arriba
 *         abajo izquierda derecha a b c d start1 start2 moneda1 moneda2 test).
 *   --traza, --traza-es: trazas de diagnóstico.
 *   --probar-estado M: al final guarda un estado, corre M frames, reinicia la máquina, carga el
 *         estado y repite: RAM, imagen y audio deben coincidir (prueba de los estados guardados).
 *   --catalog-sonido ARCHIVO: tras los N frames (driver de sonido iniciado, sin sonar), envía
 *         cada command de sonido 00-FF por separado y anota duración, bucle, pico y notas por tipo
 *         de canal. Con --output, guarda además snd_XX.wav de cada uno (Fase 6B).
 *   --probar-sonidos: al final abre el menú de sonido, reproduce una música y una voz (deben
 *         sonar), lo cierra y comprueba que el juego sigue idéntico a no haberlo abierto.
 *
 * Controles, menú y teclas fijas del mode con window: ver window.c.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dispatcher.h"
#include "neogeo.h"
#include "port.h"

void program_register(void);

static void uso(void)
{
    fprintf(stderr,
            "Uso: mslugx [--roms CARPETA] [--scale N] [--turbo N] [--sram-vacia] [--sin-audio]\n"
            "            [--date AAAA-MM-DD-hh-mm-ss-D]\n"
            "       mslugx --sin-window --frames N [--screenshots a,b,c --output CARPETA] [--script ARCHIVO]\n"
            "            [--inputs ARCHIVO] [--fingerprints ARCHIVO] [--wav ARCHIVO] [--traza ARCHIVO]\n"
            "            [--traza-es ARCHIVO DESDE_US HASTA_US]\n");
}

static int opciones(int argc, char **argv, Options *o)
{
    memset(o, 0, sizeof *o);
    o->turbo = -1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--roms") && v) { o->roms = v; i++; }
        else if (!strcmp(a, "--scale") && v) { o->scale = atoi(v); i++; }
        else if (!strcmp(a, "--turbo") && v) { o->turbo = atoi(v); i++; }
        else if (!strcmp(a, "--frames") && v) { o->frames = atol(v); i++; }
        else if (!strcmp(a, "--screenshots") && v) { o->screenshots = v; i++; }
        else if (!strcmp(a, "--output") && v) { o->output = v; i++; }
        else if (!strcmp(a, "--script") && v) { o->script = v; i++; }
        else if (!strcmp(a, "--wav") && v) { o->wav = v; i++; }
        else if (!strcmp(a, "--inputs") && v) { o->inputs = v; i++; }
        else if (!strcmp(a, "--fingerprints") && v) { o->fingerprints = v; i++; }
        else if (!strcmp(a, "--dump") && v) { o->dump = v; i++; }
        else if (!strcmp(a, "--sin-audio")) o->no_audio = 1;
        else if (!strcmp(a, "--probar-estado") && v) { o->test_state = atoi(v); i++; }
        else if (!strcmp(a, "--catalog-sonido") && v) { o->catalog = v; i++; }
        else if (!strcmp(a, "--probar-sonidos")) o->test_sounds = 1;
        else if (!strcmp(a, "--blood")) o->blood = 1;
        else if (!strcmp(a, "--traza") && v) { ng_trace = fopen(v, "w"); i++; }
        else if (!strcmp(a, "--traza-es") && i + 3 < argc) {
            /* --traza-es ARCHIVO DESDE_US HASTA_US */
            ng_trace_io = fopen(argv[i + 1], "w");
            ng_trace_from = (int64_t)(atof(argv[i + 2]) * 12.0);
            ng_trace_to = (int64_t)(atof(argv[i + 3]) * 12.0);
            i += 3;
        }
        else if (!strcmp(a, "--sin-window")) o->headless = 1;
        else if (!strcmp(a, "--sram-vacia")) o->empty_sram = 1;
        else if (!strcmp(a, "--date") && v) {
            int *f = o->date;
            if (sscanf(v, "%d-%d-%d-%d-%d-%d-%d", &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6]) != 7) return -1;
            o->has_date = 1;
            i++;
        } else return -1;
    }
    if (o->scale < 0 || o->turbo > 2) return -1;
    if (o->headless && o->frames <= 0) return -1;
    return 0;
}

/* Programa recompilado, intérprete de respaldo y encendido de la máquina (con las ROMs ya
   cargadas en ng_roms). */
void machine_start(const Options *o)
{
    static int registrado;
    if (!registrado) { program_register(); rc_interpreter_init(); registrado = 1; }
    if (o->has_date) ng_rtc_set(o->date[0], o->date[1], o->date[2], o->date[6], o->date[3], o->date[4], o->date[5]);
    ng_power_on();
}

int main(int argc, char **argv)
{
    Options o;
    if (opciones(argc, argv, &o) != 0) { uso(); return 2; }
    if (!o.headless) return window_run(&o);

    char carpeta[1024];
    if (o.roms) snprintf(carpeta, sizeof carpeta, "%s", o.roms);
    else {
        const char *base = SDL_GetBasePath();
        snprintf(carpeta, sizeof carpeta, "%sroms", base ? base : "");
    }
    char error[1200];
    if (ng_roms_load(&ng_roms, carpeta, error, sizeof error) != 0) {
        fprintf(stderr, "%s\n", error);
        fprintf(stderr, "Indica la carpeta resources\\game de tu instalación de GOG con --roms.\n");
        return 1;
    }
    cpu_turbo = o.turbo > 0 ? o.turbo : 0;
    if (headless_prepare(&o) != 0) return 1;
    machine_start(&o);
    return headless_run(&o);
}
