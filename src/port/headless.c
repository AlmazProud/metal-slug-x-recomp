/* Modo sin window: corridas deterministas para verificar contra MAME (Fases 3 y 4).
 *
 * Capturas BMP, script de inputs, inputs grabadas en MAME (por cuadro o con tiempo), fingerprints
 * por cuadro y por VBlank, trazas por instrucción y WAV del audio generado.
 */
#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dispatcher.h"
#include "neogeo.h"
#include "profile.h"
#include "port.h"
#include "sounds.h"

/* ------------------------------------------------------------------ script de inputs */
typedef struct { unsigned long long desde, hasta; uint8_t joy, sis[2], test; } Tramo;
static Tramo *tramos;
static size_t n_tramos;

static int guion_cargar(const char *ruta)
{
    FILE *f = fopen(ruta, "r");
    if (!f) { fprintf(stderr, "No se pudo abrir el script %s\n", ruta); return -1; }
    char line[512];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        Tramo tr = {0};
        int usados = 0;
        if (sscanf(line, "%llu %llu %n", &tr.desde, &tr.hasta, &usados) < 2) continue;
        for (char *tok = strtok(line + usados, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
            if (!strcmp(tok, "arriba")) tr.joy |= NG_UP;
            else if (!strcmp(tok, "abajo")) tr.joy |= NG_DOWN;
            else if (!strcmp(tok, "izquierda")) tr.joy |= NG_LEFT;
            else if (!strcmp(tok, "derecha")) tr.joy |= NG_RIGHT;
            else if (!strcmp(tok, "a")) tr.joy |= NG_A;
            else if (!strcmp(tok, "b")) tr.joy |= NG_B;
            else if (!strcmp(tok, "c")) tr.joy |= NG_C;
            else if (!strcmp(tok, "d")) tr.joy |= NG_D;
            else if (!strcmp(tok, "start1")) tr.sis[0] |= NG_START;
            else if (!strcmp(tok, "start2")) tr.sis[1] |= NG_START;
            else if (!strcmp(tok, "select1")) tr.sis[0] |= NG_SELECT;
            else if (!strcmp(tok, "moneda1")) tr.sis[0] |= NG_COIN;
            else if (!strcmp(tok, "moneda2")) tr.sis[1] |= NG_COIN;
            else if (!strcmp(tok, "test")) tr.test = 1;
            else { fprintf(stderr, "Entrada desconocida en el script: %s\n", tok); fclose(f); return -1; }
        }
        tramos = realloc(tramos, (n_tramos + 1) * sizeof *tramos);
        tramos[n_tramos++] = tr;
    }
    fclose(f);
    return 0;
}

/* Entradas del cuadro que está por ejecutarse: ng.frames, con la numeración de MAME. */
static void guion_aplicar(void)
{
    uint8_t joy = 0, s0 = 0, s1 = 0, test = 0;
    for (size_t i = 0; i < n_tramos; i++)
        if (ng.frames >= tramos[i].desde && ng.frames <= tramos[i].hasta) {
            joy |= tramos[i].joy; s0 |= tramos[i].sis[0]; s1 |= tramos[i].sis[1]; test |= tramos[i].test;
        }
    ng.joy[0] = joy;
    ng.system[0] = s0;
    ng.system[1] = s1;
    ng.test = test ? 0x00 : 0x80;
}

/* ------------------------------------------------------------------ screenshots BMP */
int save_bmp(const char *ruta)
{
    FILE *f = fopen(ruta, "wb");
    if (!f) return -1;
    const uint32_t fila = NG_WIDTH * 3, data = fila * NG_HEIGHT, total = 54 + data;
    uint8_t h[54] = {'B', 'M'};
    #define P32(o, v) do { h[o] = (uint8_t)(v); h[o + 1] = (uint8_t)((v) >> 8); h[o + 2] = (uint8_t)((v) >> 16); h[o + 3] = (uint8_t)((v) >> 24); } while (0)
    P32(2, total); P32(10, 54); P32(14, 40); P32(18, NG_WIDTH); P32(22, NG_HEIGHT);
    h[26] = 1; h[28] = 24; P32(34, data);
    #undef P32
    fwrite(h, 1, 54, f);
    uint8_t line[NG_WIDTH * 3];
    for (int y = NG_HEIGHT - 1; y >= 0; y--) {
        for (int x = 0; x < NG_WIDTH; x++) {
            uint32_t p = ng.screen[y][x];
            line[3 * x] = (uint8_t)p; line[3 * x + 1] = (uint8_t)(p >> 8); line[3 * x + 2] = (uint8_t)(p >> 16);
        }
        fwrite(line, 1, sizeof line, f);
    }
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ inputs con tiempo */
/* Cambios de inputs grabados en MAME (tools/mame/inputs.lua): "C t0 t1 PUERTO VALOR",
   con t0 = última lectura con el value anterior y t1 = primera con el nuevo (us). El cambio se
   aplica en el punto medio, en cycles del 68000. */
typedef struct { int64_t ciclo; int puerto; uint8_t value; } CambioEntrada;
static CambioEntrada *cambios;
static size_t n_cambios, i_cambio;

static int cambios_cargar(const char *ruta)
{
    FILE *f = fopen(ruta, "r");
    if (!f) return -1;
    char line[256], name[16];
    double t0, t1;
    unsigned v;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "C %lf %lf %15s %x", &t0, &t1, name, &v) != 4) continue;
        int p = !strcmp(name, "P1") ? 0 : !strcmp(name, "P2") ? 1 : !strcmp(name, "SIS") ? 2 :
                !strcmp(name, "MON") ? 3 : !strcmp(name, "TEST") ? 4 : -1;
        if (p < 0) continue;
        cambios = realloc(cambios, (n_cambios + 1) * sizeof *cambios);
        cambios[n_cambios++] = (CambioEntrada){(int64_t)((t0 + t1) / 2.0 * 12.0), p, (uint8_t)v};
    }
    fclose(f);
    return 0;
}

static void cambios_aplicar(void)
{
    while (i_cambio < n_cambios && cambios[i_cambio].ciclo <= ng_cpu.cycles) {
        uint8_t v = cambios[i_cambio].value;
        switch (cambios[i_cambio].puerto) {
        case 0: ng.raw_p1 = v; break;
        case 1: ng.raw_p2 = v; break;
        case 2: ng.raw_system = v; break;
        case 3: ng.raw_coins = v; break;
        default: ng.raw_test = v; break;
        }
        i_cambio++;
    }
}

/* ------------------------------------------------------------------ inputs crudas y fingerprints */
typedef struct { uint8_t v[5]; uint8_t hay; } Crudo;
static Crudo *crudos;
static size_t n_crudos;

static int entradas_cargar(const char *ruta)
{
    FILE *f = fopen(ruta, "r");
    if (!f) { fprintf(stderr, "No se pudo abrir %s\n", ruta); return -1; }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        unsigned long long n;
        unsigned v[5];
        if (sscanf(line, "E %llu %x %x %x %x %x", &n, &v[0], &v[1], &v[2], &v[3], &v[4]) != 6) continue;
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
        ng_before_input_read = cambios_aplicar;
    }
    ng.raw = 1;
    ng.raw_p1 = ng.raw_p2 = ng.raw_system = 0xFF;
    ng.raw_coins = 0x1F;
    ng.raw_test = 0x80;
    return 0;
}

/* Las inputs de un cuadro valen hasta que otro cuadro las cambie. */
static void entradas_aplicar(void)
{
    if (!crudos || ng.frames >= n_crudos || !crudos[ng.frames].hay) return;
    const uint8_t *v = crudos[ng.frames].v;
    ng.raw_p1 = v[0]; ng.raw_p2 = v[1]; ng.raw_system = v[2];
    ng.raw_coins = v[3]; ng.raw_test = v[4];
}

static uint32_t fnv_bytes(uint32_t h, const uint8_t *d, size_t n)
{
    for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 0x01000193u;
    return h;
}

/* PD_VOLCAR_SPRITES=cuadro,archivo: lista de los sprites dibujados en ese cuadro (box en píxeles
   visibles y registros SCB2-4, primer tile). Diagnóstico para ubicar gráficos del juego. */
static void volcar_sprites(void)
{
    const char *e = getenv("PD_VOLCAR_SPRITES");
    if (!e) return;
    unsigned long long cuadro;
    char ruta[1024];
    if (sscanf(e, "%llu,%1023s", &cuadro, ruta) != 2) return;
    ng_dbg.active = 1;
    if (ng.frames - 1 != cuadro) return;
    FILE *f = fopen(ruta, "w");
    if (!f) return;
    for (int s = 0; s < 381; s++) {
        const NgBox *c = &ng_dbg.box[s];
        if (c->x1 < c->x0) continue;
        fprintf(f, "%3d box %4d,%4d-%4d,%4d scb2 %04X scb3 %04X scb4 %04X tile0 %05X attr %04X\n", s, c->x0, c->y0,
                c->x1, c->y1, ng.vram[0x8000 | s], ng.vram[0x8200 | s], ng.vram[0x8400 | s],
                ((ng.vram[(s << 6) + 1] << 12) & 0xF0000) | ng.vram[s << 6], ng.vram[(s << 6) + 1]);
    }
    fclose(f);
}

/* PD_CAPTURAR_SPRITES=cuadro,desde,hasta,archivo.bmp: imagen solo con esos sprites (fondo magenta). */
static void capturar_sprites(void)
{
    const char *e = getenv("PD_CAPTURAR_SPRITES");
    if (!e) return;
    unsigned long long cuadro;
    int desde, hasta;
    char ruta[1024];
    if (sscanf(e, "%llu,%d,%d,%1023s", &cuadro, &desde, &hasta, ruta) != 4 || ng.frames - 1 != cuadro) return;
    static uint32_t img[NG_HEIGHT][NG_WIDTH];
    uint32_t copia[NG_HEIGHT][NG_WIDTH];
    ng_video_capture_sprites(&img[0][0], desde, hasta);
    memcpy(copia, ng.screen, sizeof copia);
    for (int y = 0; y < NG_HEIGHT; y++)
        for (int x = 0; x < NG_WIDTH; x++) ng.screen[y][x] = img[y][x] ? img[y][x] : 0xFFFF00FF;
    save_bmp(ruta);
    memcpy(ng.screen, copia, sizeof copia);
}

static FILE *archivo_huellas;
static unsigned long long n_vblanks;

static void huella_vblank(void)
{
    uint8_t pal[0x2000];
    const uint16_t *bank = ng.palette + ((uint32_t)ng.palette_bank << 12);
    for (int i = 0; i < 0x1000; i++) { pal[2 * i] = (uint8_t)(bank[i] >> 8); pal[2 * i + 1] = (uint8_t)bank[i]; }
    fprintf(archivo_huellas, "V %llu %08X %08X f=%llu pc=%06X int=%06X a5=%06X l=%02X\n", ++n_vblanks, fnv_bytes(0x811C9DC5u, ng.ram, sizeof ng.ram),
            fnv_bytes(0x811C9DC5u, pal, sizeof pal), (unsigned long long)ng.frames, ng_cpu.pc, ng_pc_vblank,
            ng_cpu.a[5], ng.ram[0x008A]);
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
    /* Imagen: mismo hash que fingerprints.lua (palabras de 64 bits, h = h * 31 + v). */
    uint64_t hp = 0;
    const uint8_t *px = (const uint8_t *)ng.screen;
    for (size_t i = 0; i < sizeof ng.screen; i += 8) {
        uint64_t v;
        memcpy(&v, px + i, 8);
        hp = hp * 31u + v;
    }
    fprintf(f, "F %llu %016llX\n", etiqueta, (unsigned long long)hp);
    uint8_t pal[0x2000];
    const uint16_t *bank = ng.palette + ((uint32_t)ng.palette_bank << 12);
    for (int i = 0; i < 0x1000; i++) { pal[2 * i] = (uint8_t)(bank[i] >> 8); pal[2 * i + 1] = (uint8_t)bank[i]; }
    fprintf(f, "%llu %08X %08X\n", etiqueta, fnv_bytes(0x811C9DC5u, ng.ram, sizeof ng.ram),
            fnv_bytes(0x811C9DC5u, pal, sizeof pal));
    if (getenv("PD_REGISTROS")) {
        fprintf(f, "R %llu %06X %04X", etiqueta, ng_cpu.pc, cpu_get_sr(&ng_cpu));
        for (int i = 0; i < 8; i++) fprintf(f, " %08X", ng_cpu.d[i]);
        for (int i = 0; i < 8; i++) fprintf(f, " %08X", ng_cpu.a[i]);
        fprintf(f, " exceso=%lld\n", (long long)(ng_cpu.cycles - (ng.frame_start + (int64_t)NG_VBLANK_LINE * NG_CYCLES_PER_LINE)));
    }
}

/* ------------------------------------------------------------------ traza por instrucción */
/* PD_TRAZA_INSTR=archivo,desde,hasta (cycles del 68000): mismo formato que
   tools/mame/cycles.lua ("C=<ciclo> <PC>:"), con todo ejecutado por el intérprete. */
static FILE *traza_instr;
static long long traza_desde, traza_hasta;
static int traza_regs;          /* PD_TRAZA_REGS=1: añade D0-D7 a cada línea */

static void trazar_instr(const Cpu68k *c)
{
    if (c->cycles >= traza_desde && c->cycles <= traza_hasta) {
        fprintf(traza_instr, "C=%lld %06X:", (long long)c->cycles, c->pc);
        if (traza_regs)
            for (int i = 0; i < 8; i++) fprintf(traza_instr, " D%d=%08X", i, c->d[i]);
        fputc('\n', traza_instr);
    }
}

/* PD_CONTAR_PC=hex: cuenta por cuadro las veces que se ejecuta esa dirección (todo por el
   intérprete). Con la output del bucle de espera del juego (0x3A42 en mslugx) mide la
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
        rc_interpreter_only = 1;
        rc_trace = contar_instr;
    }
    const char *e = getenv("PD_TRAZA_INSTR");
    if (!e) return;
    char ruta[1024];
    if (sscanf(e, "%1023[^,],%lld,%lld", ruta, &traza_desde, &traza_hasta) != 3) return;
    traza_instr = fopen(ruta, "w");
    if (!traza_instr) return;
    rc_interpreter_only = 1;
    traza_regs = getenv("PD_TRAZA_REGS") != NULL;
    rc_trace = trazar_instr;
}

/* ------------------------------------------------------------------ WAV */
static FILE *wav;
static uint32_t wav_muestras;

static void wav_cabecera(FILE *f, uint32_t muestras)
{
    uint32_t data = muestras * 4, hz = NG_AUDIO_HZ, bps = hz * 4;
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
                     16, 0, 0, 0, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 16, 0,
                     'd', 'a', 't', 'a', 0, 0, 0, 0};
    #define P32(o, v) do { h[o] = (uint8_t)(v); h[o + 1] = (uint8_t)((v) >> 8); h[o + 2] = (uint8_t)((v) >> 16); h[o + 3] = (uint8_t)((v) >> 24); } while (0)
    P32(4, 36 + data); P32(24, hz); P32(28, bps); P32(40, data);
    #undef P32
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, f);
    fseek(f, 0, SEEK_END);
}

static void wav_volcar(void)
{
    int16_t b[4096 * 2];
    uint32_t n;
    while ((n = ng_audio_read(b, 4096)) > 0) {
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

/* Huella de M frames desde el estado actual: RAM, imagen y audio de cada cuadro. */
static uint32_t huella_tramo(const Options *o, int m)
{
    uint32_t h = 0x811C9DC5u;
    int16_t b[4096 * 2];
    for (int n = 0; n < m; n++) {
        guion_aplicar();
        ng_run_frame();
        h = fnv_bytes(h, ng.ram, sizeof ng.ram);
        h = fnv_bytes(h, (const uint8_t *)ng.screen, sizeof ng.screen);
        uint32_t k;
        while ((k = ng_audio_read(b, 4096)) > 0) h = fnv_bytes(h, (const uint8_t *)b, k * 4);
    }
    (void)o;
    return h;
}

static int test_state(const Options *o)
{
    ng_before_input_read = NULL;      /* las inputs quedan fijas en el tramo */
    int16_t descarte[4096 * 2];
    while (ng_audio_read(descarte, 4096) > 0) {}   /* el audio anterior al estado no cuenta */
    size_t tam;
    uint8_t *estado = ng_state_save(&tam);
    if (!estado) return 1;
    uint32_t a = huella_tramo(o, o->test_state);
    ng_power_on();                         /* otra máquina: nada puede sobrevivir fuera del estado */
    huella_tramo(o, 3);
    if (ng_state_load(estado, tam) != 0) { printf("estado rechazado\n"); return 1; }
    uint32_t b = huella_tramo(o, o->test_state);
    free(estado);
    printf("estado: %zu bytes; %d frames tras guardar %08X, tras cargar %08X: %s\n", tam, o->test_state,
           a, b, a == b ? "IDENTICOS" : "DISTINTOS");
    return a == b ? 0 : 1;
}

/* Catálogo de los comandos de sonido (Fase 6B): cada command, solo, desde el mismo estado. */
static int catalogo_sonido(const Options *o)
{
    FILE *f = fopen(o->catalog, "w");
    if (!f) return 1;
    size_t tam;
    uint8_t *base = ng_state_save(&tam);
    if (!base) { fclose(f); return 1; }
    fprintf(f, "# cmd\tsegundos\tbucle\tpico\tfm\tssg\tadpcm_a\tadpcm_b\n");
    const long MAXIMO = 40L * NG_AUDIO_HZ;               /* las músicas en bucle se cortan aquí */
    int16_t b[4096 * 2];
    for (int c = 0; c < 256; c++) {
        ng_state_load(base, tam);
        while (ng_audio_read(b, 4096) > 0) {}
        NgNotes n0 = ng_notes;
        FILE *w = NULL;
        if (o->output) {
            char ruta[1200];
            snprintf(ruta, sizeof ruta, "%s/snd_%02X.wav", o->output, c);
            w = fopen(ruta, "wb");
            if (w) wav_cabecera(w, 0);
        }
        /* Comandos previos, separados por comas (hex), un cuadro entre cada uno. El driver de
           mslugx usa prefijos de mode: 00 = música, 10 = efectos, 11 = voces. */
        for (const char *p = getenv("PD_CATALOGO_PREFIJO"); p && *p;) {
            char *fin;
            unsigned long v = strtoul(p, &fin, 16);
            if (fin == p) break;
            ng_sound_only_command((uint8_t)v);
            ng_sound_only_advance(NG_CYCLES_PER_FRAME);
            while (ng_audio_read(b, 4096) > 0) {}
            p = *fin == ',' ? fin + 1 : fin;
        }
        ng_sound_only_command((uint8_t)c);
        long muestras = 0, ultima = -1;
        int pico = 0;
        for (;;) {
            ng_sound_only_advance(NG_CYCLES_PER_FRAME);
            uint32_t k;
            while ((k = ng_audio_read(b, 4096)) > 0) {
                for (uint32_t i = 0; i < 2 * k; i++) {
                    int v = b[i] < 0 ? -b[i] : b[i];
                    if (v > pico) pico = v;
                    if (v > 64) ultima = muestras + (long)(i / 2);
                }
                if (w) fwrite(b, 4, k, w);
                muestras += (long)k;
            }
            if (muestras >= MAXIMO) break;
            if (ultima < 0 && muestras > NG_AUDIO_HZ) break;                 /* nada en 1 s */
            if (ultima >= 0 && muestras - ultima > 3L * NG_AUDIO_HZ) break;  /* 3 s de silencio */
        }
        if (w) { wav_cabecera(w, (uint32_t)muestras); fclose(w); }
        int bucle = muestras >= MAXIMO && ultima > MAXIMO - NG_AUDIO_HZ;
        fprintf(f, "%02X\t%.2f\t%d\t%d\t%u\t%u\t%u\t%u\n", c, (ultima + 1) / (double)NG_AUDIO_HZ, bucle, pico,
                ng_notes.fm - n0.fm, ng_notes.ssg - n0.ssg, ng_notes.adpcm_a - n0.adpcm_a, ng_notes.adpcm_b - n0.adpcm_b);
        fflush(f);
    }
    free(base);
    fclose(f);
    return 0;
}

/* Energía del audio de n frames del menú de sonido (media de |muestra|). */
static double energia_sonidos(int n)
{
    int16_t b[4096 * 2];
    double suma = 0;
    long cuantas = 0;
    for (int i = 0; i < n; i++) {
        sounds_frame();
        uint32_t k;
        while ((k = ng_audio_read(b, 4096)) > 0) {
            for (uint32_t j = 0; j < 2 * k; j++) suma += b[j] < 0 ? -b[j] : b[j];
            cuantas += (long)(2 * k);
        }
    }
    return cuantas ? suma / cuantas : 0;
}

static int test_sounds(const Options *o)
{
    ng_before_input_read = NULL;
    int16_t b[4096 * 2];
    while (ng_audio_read(b, 4096) > 0) {}
    size_t tam;
    uint8_t *antes = ng_state_save(&tam);
    uint32_t a = huella_tramo(o, 300);               /* el juego sin abrir el menú */
    ng_state_load(antes, tam);

    sounds_show();
    double silencio = energia_sonidos(10);           /* tras 03 03 06: el juego queda callado */
    sounds_navigate(1);                               /* Música: segunda entrada (Misión 1) */
    sounds_navigate(4);
    double musica = energia_sonidos(300);
    sounds_navigate(3);                               /* Efectos */
    sounds_navigate(3);                               /* Voces */
    sounds_navigate(4);
    double voz = energia_sonidos(120);
    sounds_close();
    while (ng_audio_read(b, 4096) > 0) {}
    uint32_t d = huella_tramo(o, 300);               /* el juego después de cerrarlo */
    free(antes);
    int ok = a == d && musica > 200 && voz > 20 && silencio < musica / 10;
    printf("menu de sonido: silencio %.0f, musica %.0f, voz %.0f; juego despues %08X / sin menu %08X: %s\n",
           silencio, musica, voz, d, a, ok ? "OK" : "FALLA");
    return ok ? 0 : 1;
}

/* Antes de ng_power_on: el WAV necesita el audio active desde el primer cuadro. */
int headless_prepare(const Options *o)
{
    if (o->wav) {
        wav = fopen(o->wav, "wb");
        if (!wav) { fprintf(stderr, "No se pudo crear %s\n", o->wav); return 1; }
        wav_cabecera(wav, 0);
        ng_audio_enabled = 1;
    }
    if (o->test_state || o->catalog || o->test_sounds) ng_audio_enabled = 1;
    return 0;
}

int headless_run(const Options *o)
{
    if (o->blood)
        ng_forced_read[NG_FORCED_BLOOD] = (NgForcedRead){1, PROFILE_BLOOD_WORD, PROFILE_BLOOD_MASK, 0x0001};
    if (getenv("PD_DIPS")) {               /* investigación: dips de depuración del juego (profile.h) */
        uint16_t d = (uint16_t)strtoul(getenv("PD_DIPS"), NULL, 16);
        ng_forced_read[NG_FORCED_DIPS] = (NgForcedRead){1, PROFILE_DIPS_WORD, d, d};
    }
    if (getenv("PD_DESARROLLO"))           /* investigación: mode desarrollo del BIOS */
        ng_forced_read[NG_FORCED_DEVMODE] = (NgForcedRead){1, PROFILE_DEVMODE_WORD, 0xFF00, 0x0100};
    if (o->script && guion_cargar(o->script) != 0) return 2;
    if (o->inputs && entradas_cargar(o->inputs) != 0) return 2;
    traza_instr_iniciar();
    if (getenv("PD_TRAZA_LECTURAS")) {
        ng_trace_reads = fopen(getenv("PD_TRAZA_LECTURAS"), "w");
        if (!ng_trace_io) { ng_trace_from = 0; ng_trace_to = INT64_MAX; }
    }
    FILE *fingerprints = o->fingerprints ? fopen(o->fingerprints, "w") : NULL;
    if (fingerprints) { archivo_huellas = fingerprints; ng_on_vblank_ack = huella_vblank; }
    Uint64 t0 = SDL_GetTicksNS();
    for (long n = 0; n < o->frames; n++) {
        if (o->inputs) entradas_aplicar();
        else guion_aplicar();
        ng_run_frame();
        contar_fin_cuadro();
        volcar_sprites();
        capturar_sprites();
        if (fingerprints) huella(fingerprints, ng.frames - 1);
        if (pedida(o->dump, ng.frames - 1)) {
            char ruta[1200];
            snprintf(ruta, sizeof ruta, "%s/ram_port_%06llu.bin", o->output ? o->output : ".", ng.frames - 1);
            FILE *v = fopen(ruta, "wb");
            if (v) { fwrite(ng.ram, 1, sizeof ng.ram, v); fclose(v); }
        }
        if (wav) wav_volcar();
        /* Numeración de MAME (screen:frame_number()): la imagen que se completa al ejecutar
           el cuadro N (ng.frames pasa de N a N + 1) es el cuadro N. Verificado con
           screenshots: 447 de 450 frames consecutivos idénticos con esta numeración. */
        unsigned long long etiqueta = ng.frames - 1;
        if (pedida(o->screenshots, etiqueta)) {
            char ruta[1200];
            snprintf(ruta, sizeof ruta, "%s/port_%06llu.bmp", o->output ? o->output : ".", etiqueta);
            if (save_bmp(ruta) != 0) fprintf(stderr, "No se pudo escribir %s\n", ruta);
        }
    }
    double s = (double)(SDL_GetTicksNS() - t0) / 1e9;
    printf("frames: %llu en %.2f s (%.0f frames/s)  pasos recompilados: %llu  interpretados: %llu\n",
           (unsigned long long)ng.frames, s, ng.frames / (s > 0 ? s : 1),
           (unsigned long long)rc_recompiled_steps, (unsigned long long)rc_interpreted_steps);
    printf("lecturas sin mapear: %llu  escrituras sin mapear: %llu  reinicios del watchdog: %u\n",
           (unsigned long long)ng.unmapped_reads, (unsigned long long)ng.unmapped_writes,
           ng.watchdog_resets);
    if (contar_pc)
        printf("PC %06X: %llu veces en %llu frames; frames sin pasar: %llu (%.1f%%)\n", contar_pc, contar_total,
               cuadros_contados, cuadros_sin_vuelta, 100.0 * cuadros_sin_vuelta / (cuadros_contados ? cuadros_contados : 1));
    if (wav) { wav_cabecera(wav, wav_muestras); fclose(wav); }
    if (fingerprints) fclose(fingerprints);
    int r = o->test_state ? test_state(o) : 0;
    if (o->catalog) r |= catalogo_sonido(o);
    if (o->test_sounds) r |= test_sounds(o);
    ng_roms_free(&ng_roms);
    return r;
}
