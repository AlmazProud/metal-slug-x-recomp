/* Subsistema de sonido: Z80 (z80.h de floooh, zlib) + YM2610 (ymfm, BSD-3).
 *
 * Mapa del Z80 (4 MHz), según la documentación de MAME 0.289 (neogeo.cpp):
 *   0000-7FFF  M1 fijo (sin SM1: arranca desde el M1, como la AES; MAME usa una copia del M1
 *              como SM1, ver docs/ROM_MAP.md)
 *   8000-BFFF, C000-DFFF, E000-EFFF, F000-F7FF  ventanas del M1 de 16/8/4/2 KB; el banco se
 *              elige LEYENDO el puerto 0x08-0x0B (mirror 0xF0) con el banco en el byte alto:
 *              dirección = banco << (11 + ventana), ventana 0 = F000 ... 3 = 8000
 *   F800-FFFF  RAM (2 KB)
 * Puertos (byte bajo): 00 lee el comando del 68000 (y lo reconoce) / escribir lo borra;
 *   04-07 YM2610; 08 / 18 habilita / deshabilita la NMI; 0C escribe la respuesta al 68000.
 * La NMI está activa mientras haya un comando sin leer y la NMI esté habilitada; la IRQ del
 * YM2610 va a la línea INT (el programa usa IM 1).
 *
 * Tiempo: todo se mide en ciclos del reloj maestro de 24 MHz (68000 = 2, Z80 = 6, YM = 3; las
 * muestras, a 55 555 Hz como en MAME, ver HZ_MUESTRAS).
 *
 * Intercalado con el 68000, como el planificador de MAME (el oráculo):
 *   - el Z80 avanza hasta cada evento del 68000 (inicio de línea, VBlank, temporizadores del YM...),
 *     que en MAME son los límites de los tramos de ejecución;
 *   - un comando del 68000 sincroniza al Z80 en ese instante y abre una ventana de 50 us de
 *     intercalado fino (MAME: perfect_quantum en audio_command_w);
 *   - fuera de esa ventana, el 68000 ve la respuesta del Z80 recién en el tramo siguiente
 *     (medido: 15,8-33,9 us de retraso, herramientas/mame/latencia_z80.lua).
 */
#include "neogeo.h"
#include "ym2610.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "z80.h"

#define MAESTRO_POR_68K 2
#define MAESTRO_POR_Z80 6
#define MAESTRO_POR_YM 3
/* Muestras: MAME genera el flujo del YM2610 a 55 555 Hz exactos (la frecuencia es entera), no a
   8 MHz / 144 = 55 555,55 Hz. Como cada muestra hace avanzar el estado del YM (envolventes,
   ADPCM), se usa la misma cadencia: la muestra k termina en ceil((k + 1) * 24e6 / 55 555). */
#define HZ_MUESTRAS 55555
#define MAESTRO_HZ 24000000
#define VENTANA_FINA 600                 /* ciclos del 68000 = 50 us */
#define AUDIO_CAP 65536                  /* muestras estéreo en el búfer circular */

static struct {
    z80_t z80;
    uint64_t pines;
    int64_t t;                           /* ciclo maestro alcanzado por el Z80 */
    int64_t ahora;                       /* ciclo maestro del evento en curso (callbacks del YM) */
    uint8_t ram[0x800];
    uint8_t banco[4];
    uint8_t comando, pendiente, nmi_habilitada, respuesta;
    int irq_ym;
    int64_t temporizador[2];             /* vencimiento (maestro), -1 = detenido */
    int64_t ocupado_fin;
    int64_t proxima_muestra;
    int64_t n_muestras;
    int64_t fina_hasta;                  /* ciclo del 68000 */
    int16_t audio[AUDIO_CAP][2];
    uint32_t escritura, lectura;
} s;

int ng_audio_activo;
void (*ng_al_comando_sonido)(uint8_t comando);
NgNotas ng_notas;
static uint8_t dir_ym[2];                /* último registro elegido en cada banco (ng_notas) */

/* ------------------------------------------------------------------ YM2610 */
void ng_ym_temporizador(uint32_t n, int32_t ciclos)
{
    s.temporizador[n & 1] = ciclos < 0 ? -1 : s.ahora + (int64_t)ciclos * MAESTRO_POR_YM;
}

void ng_ym_ocupado_hasta(uint32_t ciclos) { s.ocupado_fin = s.ahora + (int64_t)ciclos * MAESTRO_POR_YM; }
int ng_ym_ocupado(void) { return s.ahora < s.ocupado_fin; }
void ng_ym_irq(int activa) { s.irq_ym = activa; }

uint8_t ng_ym_rom(uint32_t d)
{
    d &= 0xFFFFFF;
    return d < ng_roms.adpcm_tam ? ng_roms.adpcm[d] : 0;
}

static int16_t saturar(double v)
{
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

static void muestra(void)
{
    int32_t o[3];
    ym_generar(o);
    if (!ng_audio_activo) return;
    /* Mezcla de MAME (neogeo_stereo): SSG x0,84 a ambos canales; FM izq./der. x0,98. */
    uint32_t i = s.escritura % AUDIO_CAP;
    s.audio[i][0] = saturar(0.98 * o[0] + 0.84 * o[2]);
    s.audio[i][1] = saturar(0.98 * o[1] + 0.84 * o[2]);
    s.escritura++;
    if (s.escritura - s.lectura > AUDIO_CAP) s.lectura = s.escritura - AUDIO_CAP;   /* descarta lo viejo */
}

uint32_t ng_audio_leer(int16_t *destino, uint32_t max)
{
    uint32_t n = 0;
    while (n < max && s.lectura != s.escritura) {
        uint32_t i = s.lectura++ % AUDIO_CAP;
        destino[2 * n] = s.audio[i][0];
        destino[2 * n + 1] = s.audio[i][1];
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------ bus del Z80 */
static FILE *traza_z80, *traza_ym;
static int64_t traza_z80_desde, traza_z80_hasta;
static int traza_z80_lista;

static uint8_t mem_leer(uint16_t a)
{
    const uint8_t *m1 = ng_roms.z80;
    if (a < 0x8000) return m1[a];
    if (a < 0xC000) return m1[(((uint32_t)s.banco[3] << 14) + (a & 0x3FFF)) & 0x1FFFF];
    if (a < 0xE000) return m1[(((uint32_t)s.banco[2] << 13) + (a & 0x1FFF)) & 0x1FFFF];
    if (a < 0xF000) return m1[(((uint32_t)s.banco[1] << 12) + (a & 0x0FFF)) & 0x1FFFF];
    if (a < 0xF800) return m1[(((uint32_t)s.banco[0] << 11) + (a & 0x07FF)) & 0x1FFFF];
    return s.ram[a & 0x7FF];
}

static uint8_t puerto_leer(uint16_t p)
{
    uint8_t bajo = (uint8_t)p;
    if (bajo == 0x00) { s.pendiente = 0; return s.comando; }
    if (bajo >= 0x04 && bajo <= 0x07) return ym_leer(bajo & 3);
    if ((bajo & 0x0F) >= 0x08 && (bajo & 0x0F) <= 0x0B) { s.banco[bajo & 3] = (uint8_t)(p >> 8); return 0; }
    return 0;
}

static void puerto_escribir(uint16_t p, uint8_t v)
{
    uint8_t bajo = (uint8_t)p;
    if (traza_ym && s.t >= traza_z80_desde && s.t <= traza_z80_hasta)
        fprintf(traza_ym, "%.3f %02X %02X\n", s.t / 24.0, bajo, v);
    if (bajo == 0x00) s.comando = 0;
    else if (bajo >= 0x04 && bajo <= 0x07) {
        int banco = (bajo >> 1) & 1;
        if (!(bajo & 1)) dir_ym[banco] = v;
        else if (banco == 0) {
            if (dir_ym[0] == 0x28 && (v & 0xF0)) ng_notas.fm++;               /* key on FM */
            else if (dir_ym[0] >= 0x08 && dir_ym[0] <= 0x0A && (v & 0x1F)) ng_notas.ssg++;
            else if (dir_ym[0] == 0x10 && (v & 0x80)) ng_notas.adpcm_b++;     /* inicio ADPCM-B */
        } else if (dir_ym[1] == 0x00 && !(v & 0x80) && (v & 0x3F)) {
            ng_notas.adpcm_a++;                                              /* key on ADPCM-A */
        }
        ym_escribir(bajo & 3, v);
    }
    else if (bajo == 0x08) s.nmi_habilitada = 1;
    else if (bajo == 0x18) s.nmi_habilitada = 0;
    else if (bajo == 0x0C) {
        s.respuesta = v;
        if (ng_traza) fprintf(ng_traza, "%.3f RESP %02X\n", s.t / 24.0, v);
    }
}

/* (declaradas arriba) Traza por instrucción del Z80 (diagnóstico): PD_TRAZA_Z80=archivo,desde_us,hasta_us. Formato
   "C=<ciclo del Z80> <PC>:", comparable con la traza del depurador de MAME (totalcycles). */
static void traza_z80_iniciar(void)
{
    traza_z80_lista = 1;
    const char *e = getenv("PD_TRAZA_Z80");
    double d, h;
    char ruta[1024];
    if (!e || sscanf(e, "%1023[^,],%lf,%lf", ruta, &d, &h) != 3) return;
    if (getenv("PD_TRAZA_YM")) traza_ym = fopen(ruta, "w");   /* solo escrituras de puertos */
    else traza_z80 = fopen(ruta, "w");
    traza_z80_desde = (int64_t)(d * 24.0);
    traza_z80_hasta = (int64_t)(h * 24.0);
}

static void tick_z80(void)
{
    if (!traza_z80_lista) traza_z80_iniciar();
    if (traza_z80 && z80_opdone(&s.z80) && s.t >= traza_z80_desde && s.t <= traza_z80_hasta)
        fprintf(traza_z80, "C=%lld %04X:\n", (long long)(s.t / MAESTRO_POR_Z80), s.z80.pc);
    uint64_t p = s.pines & ~(Z80_NMI | Z80_INT);
    if (s.pendiente && s.nmi_habilitada) p |= Z80_NMI;
    if (s.irq_ym) p |= Z80_INT;
    p = z80_tick(&s.z80, p);
    if (p & Z80_MREQ) {
        uint16_t a = Z80_GET_ADDR(p);
        if (p & Z80_RD) { Z80_SET_DATA(p, mem_leer(a)); }
        else if ((p & Z80_WR) && a >= 0xF800) s.ram[a & 0x7FF] = Z80_GET_DATA(p);
    } else if (p & Z80_IORQ) {
        uint16_t a = Z80_GET_ADDR(p);
        if (p & Z80_M1) { Z80_SET_DATA(p, 0xFF); }        /* reconocimiento de INT (IM 1) */
        else if (p & Z80_RD) { Z80_SET_DATA(p, puerto_leer(a)); }
        else if (p & Z80_WR) puerto_escribir(a, Z80_GET_DATA(p));
    }
    s.pines = p;
}

/* Temporizadores del YM y muestras hasta el ciclo maestro t. */
static void perifericos_hasta(int64_t t)
{
    for (;;) {
        int64_t prox = s.proxima_muestra;
        int n = -1;
        for (int i = 0; i < 2; i++)
            if (s.temporizador[i] >= 0 && s.temporizador[i] < prox) { prox = s.temporizador[i]; n = i; }
        if (prox > t) break;
        s.ahora = prox;
        if (n >= 0) {
            s.temporizador[n] = -1;
            ym_temporizador_vencido((uint32_t)n);
        } else {
            muestra();
            s.n_muestras++;
            s.proxima_muestra = ((s.n_muestras + 1) * MAESTRO_HZ + HZ_MUESTRAS - 1) / HZ_MUESTRAS;
        }
    }
}

void ng_sonido_hasta(int64_t ciclos_68k)
{
    int64_t fin = ciclos_68k * MAESTRO_POR_68K;
    while (s.t + MAESTRO_POR_Z80 <= fin) {
        s.ahora = s.t;
        tick_z80();
        s.t += MAESTRO_POR_Z80;
        perifericos_hasta(s.t);
    }
}

int64_t ng_sonido_proximo_evento(void)
{
    int64_t t = INT64_MAX;
    for (int i = 0; i < 2; i++)
        if (s.temporizador[i] >= 0 && s.temporizador[i] < t) t = s.temporizador[i];
    return t == INT64_MAX ? t : (t + MAESTRO_POR_68K - 1) / MAESTRO_POR_68K;
}

/* ------------------------------------------------------------------ lado del 68000 */
void ng_sonido_comando(uint8_t v)
{
    if (ng_traza)
        fprintf(ng_traza, "%.3f f=%llu linea=%lld pc=%06X CMD %02X\n", ng_cpu.ciclos / 12.0,
                (unsigned long long)ng.cuadros, (long long)((ng_cpu.ciclos - ng.inicio_cuadro) / NG_CICLOS_LINEA), ng_cpu.pc, v);
    ng_sonido_hasta(ng_cpu.ciclos);
    s.comando = v;
    s.pendiente = 1;
    s.fina_hasta = ng_cpu.ciclos + VENTANA_FINA;
    if (ng_al_comando_sonido) ng_al_comando_sonido(v);
}

void ng_sonido_solo_comando(uint8_t comando)
{
    ng_cpu.ciclos = s.t / MAESTRO_POR_68K;
    ng_sonido_comando(comando);
}

void ng_sonido_solo_avanzar(int64_t ciclos_68k)
{
    ng_sonido_hasta(s.t / MAESTRO_POR_68K + ciclos_68k);
}

uint8_t ng_sonido_respuesta(void)
{
    if (ng_cpu.ciclos <= s.fina_hasta) {
        ng_sonido_hasta(ng_cpu.ciclos);
    } else {
        /* La lectura de bus ocurre cerca del final de la instrucción (antes de la última
           búsqueda anticipada): ng_cpu.ciclos ya incluye la instrucción entera. Si un límite de
           tramo de MAME cae antes de esa lectura, el Z80 ya corrió hasta él (medido en Fase 4:
           MAME ve la respuesta en la lectura que empieza 5 ciclos antes de un inicio de línea). */
        int64_t lectura = ng_cpu.ciclos - 8, limite = ng_proximo_limite();
        if (limite <= lectura) ng_sonido_hasta(limite);
    }
    static int visto = -1;
    if (ng_traza && s.respuesta != visto) {
        fprintf(ng_traza, "%.3f VISTO %02X\n", ng_cpu.ciclos / 12.0, s.respuesta);
        visto = s.respuesta;
    }
    return s.respuesta;
}

void ng_sonido_estado(NgEstado *e)
{
    ng_estado_bloque(e, &s, sizeof s);
    uint32_t n = ym_estado_tam();
    uint32_t n_guardado = n;
    ng_estado_bloque(e, &n_guardado, sizeof n_guardado);
    if (e->guardando) {
        if (e->datos) ym_estado_guardar(e->datos + e->pos);
    } else {
        s.lectura = s.escritura;                 /* el audio pendiente no se repite */
        if (n_guardado == n && e->pos + n <= e->tam) ym_estado_cargar(e->datos + e->pos, n);
    }
    e->pos += n;
}

void ng_sonido_iniciar(void)
{
    int activo = ng_audio_activo;
    memset(&s, 0, sizeof s);
    ng_audio_activo = activo;
    s.pines = z80_init(&s.z80);
    /* Bancos iniciales de MAME (identidad); el NEO-ZMC real empieza en 0, pero el programa de
       sonido los fija antes de usarlos. */
    s.banco[0] = 0x1E; s.banco[1] = 0x0E; s.banco[2] = 0x06; s.banco[3] = 0x02;
    s.temporizador[0] = s.temporizador[1] = -1;
    s.proxima_muestra = (MAESTRO_HZ + HZ_MUESTRAS - 1) / HZ_MUESTRAS;
    ym_reiniciar();
}
