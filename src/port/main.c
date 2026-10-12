/* Metal Slug X, port nativo: opciones de línea de comandos y arranque.
 *
 * Uso:
 *   mslugx [--roms CARPETA] [--escala N] [--turbo N] [--sram-vacia] [--sin-audio]
 *          [--fecha AAAA-MM-DD-hh-mm-ss-D]
 *   mslugx --sin-ventana --cuadros N [--capturas a,b,c --salida CARPETA] ...
 *
 * --roms: carpeta resources\game de la instalación de GOG. Sin esta opción, el modo con ventana
 *         la busca solo (ventana.c); el modo sin ventana usa "roms" junto al ejecutable. Los
 *         archivos se validan por SHA-256.
 * --turbo: 0 = como el original; 1 / 2 = 68000 al doble / cuádruple (sin la ralentización
 *         original). Con ventana, por defecto se usa el de mslugx.ini.
 * --sin-ventana: corre N cuadros lo más rápido posible (verificación contra MAME, sin_ventana.c):
 *   --capturas: guarda BMP de los cuadros pedidos (herramientas/comparar_capturas.py).
 *   --wav: guarda el audio generado (estéreo, 16 bits, 55 555 Hz).
 *   --entradas: entradas grabadas en MAME (herramientas/mame/entradas.lua, líneas "C" con tiempo,
 *         o "E cuadro P1 P2 SIS MON TEST"). --huellas: huellas por cuadro y por VBlank (Fase 4).
 *   --guion: entradas por tramos: "desde hasta entrada..." (cuadros inclusive; entradas: arriba
 *         abajo izquierda derecha a b c d start1 start2 moneda1 moneda2 test).
 *   --traza, --traza-es: trazas de diagnóstico.
 *   --probar-estado M: al final guarda un estado, corre M cuadros, reinicia la máquina, carga el
 *         estado y repite: RAM, imagen y audio deben coincidir (prueba de los estados guardados).
 *
 * Controles, menú y teclas fijas del modo con ventana: ver ventana.c.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "despachador.h"
#include "neogeo.h"
#include "port.h"

void programa_registrar(void);

static void uso(void)
{
    fprintf(stderr,
            "Uso: mslugx [--roms CARPETA] [--escala N] [--turbo N] [--sram-vacia] [--sin-audio]\n"
            "            [--fecha AAAA-MM-DD-hh-mm-ss-D]\n"
            "       mslugx --sin-ventana --cuadros N [--capturas a,b,c --salida CARPETA] [--guion ARCHIVO]\n"
            "            [--entradas ARCHIVO] [--huellas ARCHIVO] [--wav ARCHIVO] [--traza ARCHIVO]\n"
            "            [--traza-es ARCHIVO DESDE_US HASTA_US]\n");
}

static int opciones(int argc, char **argv, Opciones *o)
{
    memset(o, 0, sizeof *o);
    o->turbo = -1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--roms") && v) { o->roms = v; i++; }
        else if (!strcmp(a, "--escala") && v) { o->escala = atoi(v); i++; }
        else if (!strcmp(a, "--turbo") && v) { o->turbo = atoi(v); i++; }
        else if (!strcmp(a, "--cuadros") && v) { o->cuadros = atol(v); i++; }
        else if (!strcmp(a, "--capturas") && v) { o->capturas = v; i++; }
        else if (!strcmp(a, "--salida") && v) { o->salida = v; i++; }
        else if (!strcmp(a, "--guion") && v) { o->guion = v; i++; }
        else if (!strcmp(a, "--wav") && v) { o->wav = v; i++; }
        else if (!strcmp(a, "--entradas") && v) { o->entradas = v; i++; }
        else if (!strcmp(a, "--huellas") && v) { o->huellas = v; i++; }
        else if (!strcmp(a, "--volcar") && v) { o->volcar = v; i++; }
        else if (!strcmp(a, "--sin-audio")) o->sin_audio = 1;
        else if (!strcmp(a, "--probar-estado") && v) { o->probar_estado = atoi(v); i++; }
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
    if (o->escala < 0 || o->turbo > 2) return -1;
    if (o->sin_ventana && o->cuadros <= 0) return -1;
    return 0;
}

/* Programa recompilado, intérprete de respaldo y encendido de la máquina (con las ROMs ya
   cargadas en ng_roms). */
void maquina_arrancar(const Opciones *o)
{
    static int registrado;
    if (!registrado) { programa_registrar(); rc_interprete_iniciar(); registrado = 1; }
    if (o->con_fecha) ng_rtc_fijar(o->fecha[0], o->fecha[1], o->fecha[2], o->fecha[6], o->fecha[3], o->fecha[4], o->fecha[5]);
    ng_encender();
}

int main(int argc, char **argv)
{
    Opciones o;
    if (opciones(argc, argv, &o) != 0) { uso(); return 2; }
    if (!o.sin_ventana) return ventana_correr(&o);

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
        return 1;
    }
    cpu_turbo = o.turbo > 0 ? o.turbo : 0;
    if (sin_ventana_preparar(&o) != 0) return 1;
    maquina_arrancar(&o);
    return sin_ventana_correr(&o);
}
