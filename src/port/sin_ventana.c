/* Modo sin ventana: corridas deterministas para verificar contra MAME (Fases 3 y 4).
 *
 * Capturas BMP, guion de entradas, entradas grabadas en MAME (por cuadro o con tiempo), huellas
 * por cuadro y por VBlank, trazas por instrucción y WAV del audio generado.
 */
#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "despachador.h"
#include "neogeo.h"
#include "port.h"

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
int guardar_bmp(const char *ruta)
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

/* ------------------------------------------------------------------ entradas con tiempo */
/* Cambios de entradas grabados en MAME (herramientas/mame/entradas.lua): "C t0 t1 PUERTO VALOR",
   con t0 = última lectura con el valor anterior y t1 = primera con el nuevo (us). El cambio se
   aplica en el punto medio, en ciclos del 68000. */
typedef struct { int64_t ciclo; int puerto; uint8_t valor; } CambioEntrada;
static CambioEntrada *cambios;
static size_t n_cambios, i_cambio;

static int cambios_cargar(const char *ruta)
{
    FILE *f = fopen(ruta, "r");
    if (!f) return -1;
    char linea[256], nombre[16];
    double t0, t1;
    unsigned v;
    while (fgets(linea, sizeof linea, f)) {
        if (sscanf(linea, "C %lf %lf %15s %x", &t0, &t1, nombre, &v) != 4) continue;
        int p = !strcmp(nombre, "P1") ? 0 : !strcmp(nombre, "P2") ? 1 : !strcmp(nombre, "SIS") ? 2 :
                !strcmp(nombre, "MON") ? 3 : !strcmp(nombre, "TEST") ? 4 : -1;
        if (p < 0) continue;
        cambios = realloc(cambios, (n_cambios + 1) * sizeof *cambios);
        cambios[n_cambios++] = (CambioEntrada){(int64_t)((t0 + t1) / 2.0 * 12.0), p, (uint8_t)v};
    }
    fclose(f);
    return 0;
}

static void cambios_aplicar(void)
{
    while (i_cambio < n_cambios && cambios[i_cambio].ciclo <= ng_cpu.ciclos) {
        uint8_t v = cambios[i_cambio].valor;
        switch (cambios[i_cambio].puerto) {
        case 0: ng.crudo_p1 = v; break;
        case 1: ng.crudo_p2 = v; break;
        case 2: ng.crudo_sistema = v; break;
        case 3: ng.crudo_monedas = v; break;
        default: ng.crudo_test = v; break;
        }
        i_cambio++;
    }
}

/* ------------------------------------------------------------------ entradas crudas y huellas */
typedef struct { uint8_t v[5]; uint8_t hay; } Crudo;
static Crudo *crudos;
static size_t n_crudos;

static int entradas_cargar(const char *ruta)
{
    FILE *f = fopen(ruta, "r");
    if (!f) { fprintf(stderr, "No se pudo abrir %s\n", ruta); return -1; }
    char linea[256];
    while (fgets(linea, sizeof linea, f)) {
        unsigned long long n;
        unsigned v[5];
        if (sscanf(linea, "E %llu %x %x %x %x %x", &n, &v[0], &v[1], &v[2], &v[3], &v[4]) != 6) continue;
        if (n >= n_crudos) {
            size_t nuevo = (size_t)n + 1024;
            crudos = realloc(crudos, nuevo * sizeof *crudos);
            memset(crudos + n_crudos, 0, (nuevo - n_crudos) * sizeof *crudos);
            n_crudos = nuevo;
        }
        for (int i = 0; i < 5; i++) crudos[n].v[i] = (uint8_t)v[i];
        crudos[n].hay = 1;
    }
    fclose(f);
    /* El mismo archivo puede traer cambios con tiempo (líneas C): tienen prioridad. */
    if (cambios_cargar(ruta) == 0 && n_cambios) {
        free(crudos); crudos = NULL; n_crudos = 0;
        ng_antes_de_leer_entradas = cambios_aplicar;
    }
    ng.crudo = 1;
    ng.crudo_p1 = ng.crudo_p2 = ng.crudo_sistema = 0xFF;
    ng.crudo_monedas = 0x1F;
    ng.crudo_test = 0x80;
    return 0;
}

/* Las entradas de un cuadro valen hasta que otro cuadro las cambie. */
static void entradas_aplicar(void)
{
    if (!crudos || ng.cuadros >= n_crudos || !crudos[ng.cuadros].hay) return;
    const uint8_t *v = crudos[ng.cuadros].v;
    ng.crudo_p1 = v[0]; ng.crudo_p2 = v[1]; ng.crudo_sistema = v[2];
    ng.crudo_monedas = v[3]; ng.crudo_test = v[4];
}

static uint32_t fnv_bytes(uint32_t h, const uint8_t *d, size_t n)
{
    for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 0x01000193u;
    return h;
}

static FILE *archivo_huellas;
static unsigned long long n_vblanks;

static void huella_vblank(void)
{
    uint8_t pal[0x2000];
    const uint16_t *banco = ng.paleta + ((uint32_t)ng.banco_paleta << 12);
    for (int i = 0; i < 0x1000; i++) { pal[2 * i] = (uint8_t)(banco[i] >> 8); pal[2 * i + 1] = (uint8_t)banco[i]; }
    fprintf(archivo_huellas, "V %llu %08X %08X f=%llu pc=%06X int=%06X a5=%06X b=%02X\n", ++n_vblanks, fnv_bytes(0x811C9DC5u, ng.ram, sizeof ng.ram),
            fnv_bytes(0x811C9DC5u, pal, sizeof pal), (unsigned long long)ng.cuadros, ng_cpu.pc, ng_pc_vblank,
            ng_cpu.a[5], ng.ram[(ng_cpu.a[5] - 0x7F75) & 0xFFFF]);
    const char *lista = getenv("PD_VOLCAR_V");
    if (lista) {
        for (const char *p = lista; *p;) {
            char *fin;
            unsigned long long v = strtoull(p, &fin, 10);
            if (fin == p) break;
            if (v == n_vblanks) {
                char ruta[1200];
                snprintf(ruta, sizeof ruta, "%s/ram_port_v%06llu.bin", getenv("PD_DIR_VOLCADOS") ? getenv("PD_DIR_VOLCADOS") : ".", v);
                FILE *f = fopen(ruta, "wb");
                if (f) { fwrite(ng.ram, 1, sizeof ng.ram, f); fclose(f); }
            }
            p = *fin == ',' ? fin + 1 : fin;
        }
    }
}

static void huella(FILE *f, unsigned long long etiqueta)
{
    /* Imagen: mismo hash que huellas.lua (palabras de 64 bits, h = h * 31 + v). */
    uint64_t hp = 0;
    const uint8_t *px = (const uint8_t *)ng.pantalla;
    for (size_t i = 0; i < sizeof ng.pantalla; i += 8) {
        uint64_t v;
        memcpy(&v, px + i, 8);
        hp = hp * 31u + v;
    }
    fprintf(f, "F %llu %016llX\n", etiqueta, (unsigned long long)hp);
    uint8_t pal[0x2000];
    const uint16_t *banco = ng.paleta + ((uint32_t)ng.banco_paleta << 12);
    for (int i = 0; i < 0x1000; i++) { pal[2 * i] = (uint8_t)(banco[i] >> 8); pal[2 * i + 1] = (uint8_t)banco[i]; }
    fprintf(f, "%llu %08X %08X\n", etiqueta, fnv_bytes(0x811C9DC5u, ng.ram, sizeof ng.ram),
            fnv_bytes(0x811C9DC5u, pal, sizeof pal));
    if (getenv("PD_REGISTROS")) {
        fprintf(f, "R %llu %06X %04X", etiqueta, ng_cpu.pc, cpu_get_sr(&ng_cpu));
        for (int i = 0; i < 8; i++) fprintf(f, " %08X", ng_cpu.d[i]);
        for (int i = 0; i < 8; i++) fprintf(f, " %08X", ng_cpu.a[i]);
        fprintf(f, " exceso=%lld\n", (long long)(ng_cpu.ciclos - (ng.inicio_cuadro + (int64_t)NG_LINEA_VBLANK * NG_CICLOS_LINEA)));
    }
}

/* ------------------------------------------------------------------ traza por instrucción */
/* PD_TRAZA_INSTR=archivo,desde,hasta (ciclos del 68000): mismo formato que
   herramientas/mame/ciclos.lua ("C=<ciclo> <PC>:"), con todo ejecutado por el intérprete. */
static FILE *traza_instr;
static long long traza_desde, traza_hasta;
static int traza_regs;          /* PD_TRAZA_REGS=1: añade D0-D7 a cada línea */

static void trazar_instr(const Cpu68k *c)
{
    if (c->ciclos >= traza_desde && c->ciclos <= traza_hasta) {
        fprintf(traza_instr, "C=%lld %06X:", (long long)c->ciclos, c->pc);
        if (traza_regs)
            for (int i = 0; i < 8; i++) fprintf(traza_instr, " D%d=%08X", i, c->d[i]);
        fputc('\n', traza_instr);
    }
}

/* PD_CONTAR_PC=hex: cuenta por cuadro las veces que se ejecuta esa dirección (todo por el
   intérprete). Con la salida del bucle de espera del juego (0x3A42 en mslugx) mide la
   ralentización: un cuadro sin ninguna vuelta del bucle principal es un cuadro perdido. */
static uint32_t contar_pc;
static unsigned long long contar_total, contar_cuadro, cuadros_sin_vuelta, cuadros_contados;

static void contar_instr(const Cpu68k *c)
{
    if (c->pc == contar_pc) contar_cuadro++;
}

static void contar_fin_cuadro(void)
{
    if (!contar_pc) return;
    cuadros_contados++;
    contar_total += contar_cuadro;
    if (!contar_cuadro) cuadros_sin_vuelta++;
    static FILE *detalle;                   /* PD_CONTAR_ARCHIVO: una cifra por cuadro */
    if (!detalle && getenv("PD_CONTAR_ARCHIVO")) detalle = fopen(getenv("PD_CONTAR_ARCHIVO"), "w");
    if (detalle) fputc(contar_cuadro > 9 ? '+' : (int)('0' + contar_cuadro), detalle);
    contar_cuadro = 0;
}

static void traza_instr_iniciar(void)
{
    const char *cp = getenv("PD_CONTAR_PC");
    if (cp) {
        contar_pc = (uint32_t)strtoul(cp, NULL, 16);
        rc_solo_interprete = 1;
        rc_traza = contar_instr;
    }
    const char *e = getenv("PD_TRAZA_INSTR");
    if (!e) return;
    char ruta[1024];
    if (sscanf(e, "%1023[^,],%lld,%lld", ruta, &traza_desde, &traza_hasta) != 3) return;
    traza_instr = fopen(ruta, "w");
    if (!traza_instr) return;
    rc_solo_interprete = 1;
    traza_regs = getenv("PD_TRAZA_REGS") != NULL;
    rc_traza = trazar_instr;
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

/* Huella de M cuadros desde el estado actual: RAM, imagen y audio de cada cuadro. */
static uint32_t huella_tramo(const Opciones *o, int m)
{
    uint32_t h = 0x811C9DC5u;
    int16_t b[4096 * 2];
    for (int n = 0; n < m; n++) {
        guion_aplicar();
        ng_ejecutar_cuadro();
        h = fnv_bytes(h, ng.ram, sizeof ng.ram);
        h = fnv_bytes(h, (const uint8_t *)ng.pantalla, sizeof ng.pantalla);
        uint32_t k;
        while ((k = ng_audio_leer(b, 4096)) > 0) h = fnv_bytes(h, (const uint8_t *)b, k * 4);
    }
    (void)o;
    return h;
}

static int probar_estado(const Opciones *o)
{
    ng_antes_de_leer_entradas = NULL;      /* las entradas quedan fijas en el tramo */
    int16_t descarte[4096 * 2];
    while (ng_audio_leer(descarte, 4096) > 0) {}   /* el audio anterior al estado no cuenta */
    size_t tam;
    uint8_t *estado = ng_estado_guardar(&tam);
    if (!estado) return 1;
    uint32_t a = huella_tramo(o, o->probar_estado);
    ng_encender();                         /* otra máquina: nada puede sobrevivir fuera del estado */
    huella_tramo(o, 3);
    if (ng_estado_cargar(estado, tam) != 0) { printf("estado rechazado\n"); return 1; }
    uint32_t b = huella_tramo(o, o->probar_estado);
    free(estado);
    printf("estado: %zu bytes; %d cuadros tras guardar %08X, tras cargar %08X: %s\n", tam, o->probar_estado,
           a, b, a == b ? "IDENTICOS" : "DISTINTOS");
    return a == b ? 0 : 1;
}

/* Antes de ng_encender: el WAV necesita el audio activo desde el primer cuadro. */
int sin_ventana_preparar(const Opciones *o)
{
    if (o->wav) {
        wav = fopen(o->wav, "wb");
        if (!wav) { fprintf(stderr, "No se pudo crear %s\n", o->wav); return 1; }
        wav_cabecera(wav, 0);
        ng_audio_activo = 1;
    }
    if (o->probar_estado) ng_audio_activo = 1;
    return 0;
}

int sin_ventana_correr(const Opciones *o)
{
    if (o->guion && guion_cargar(o->guion) != 0) return 2;
    if (o->entradas && entradas_cargar(o->entradas) != 0) return 2;
    traza_instr_iniciar();
    if (getenv("PD_TRAZA_LECTURAS")) {
        ng_traza_lecturas = fopen(getenv("PD_TRAZA_LECTURAS"), "w");
        if (!ng_traza_es) { ng_traza_desde = 0; ng_traza_hasta = INT64_MAX; }
    }
    FILE *huellas = o->huellas ? fopen(o->huellas, "w") : NULL;
    if (huellas) { archivo_huellas = huellas; ng_al_reconocer_vblank = huella_vblank; }
    Uint64 t0 = SDL_GetTicksNS();
    for (long n = 0; n < o->cuadros; n++) {
        if (o->entradas) entradas_aplicar();
        else guion_aplicar();
        ng_ejecutar_cuadro();
        contar_fin_cuadro();
        if (huellas) huella(huellas, ng.cuadros - 1);
        if (pedida(o->volcar, ng.cuadros - 1)) {
            char ruta[1200];
            snprintf(ruta, sizeof ruta, "%s/ram_port_%06llu.bin", o->salida ? o->salida : ".", ng.cuadros - 1);
            FILE *v = fopen(ruta, "wb");
            if (v) { fwrite(ng.ram, 1, sizeof ng.ram, v); fclose(v); }
        }
        if (wav) wav_volcar();
        /* Numeración de MAME (screen:frame_number()): la imagen que se completa al ejecutar
           el cuadro N (ng.cuadros pasa de N a N + 1) es el cuadro N. Verificado con
           capturas: 447 de 450 cuadros consecutivos idénticos con esta numeración. */
        unsigned long long etiqueta = ng.cuadros - 1;
        if (pedida(o->capturas, etiqueta)) {
            char ruta[1200];
            snprintf(ruta, sizeof ruta, "%s/port_%06llu.bmp", o->salida ? o->salida : ".", etiqueta);
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
    if (contar_pc)
        printf("PC %06X: %llu veces en %llu cuadros; cuadros sin pasar: %llu (%.1f%%)\n", contar_pc, contar_total,
               cuadros_contados, cuadros_sin_vuelta, 100.0 * cuadros_sin_vuelta / (cuadros_contados ? cuadros_contados : 1));
    if (wav) { wav_cabecera(wav, wav_muestras); fclose(wav); }
    if (huellas) fclose(huellas);
    int r = o->probar_estado ? probar_estado(o) : 0;
    ng_roms_liberar(&ng_roms);
    return r;
}
