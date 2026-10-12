/* Lockstep del programa recompilado contra Musashi puro, desde el reset y con la misma máquina.
 *
 * 1) Corre el programa recompilado (con respaldo en Musashi) hasta N ciclos y registra un punto
 *    de control en cada despacho: (ciclos, PC, hash de registros).
 * 2) Corre Musashi puro desde el mismo reset, instrucción por instrucción, y en cada punto de
 *    control exige el mismo PC y los mismos registros en el mismo ciclo.
 * 3) Compara la memoria final (RAM, SRAM, paleta, VRAM).
 *
 * VBlank (IRQ1, autovector) cada cuadro en la línea 248. El lado recompilado la entrega en un
 * punto de despacho y registra (ciclo, PC); Musashi la recibe exactamente en ese mismo punto.
 * La entrada a la interrupción se emula igual en ambos lados (44 ciclos, como Musashi).
 *
 * Uso: lockstep <mslugx_game_m68k> <mslugx_bios_m68k> [ciclos] [volcado de MAME]
 * Con un volcado (herramientas/mame/volcar_estado.lua) ambos lados arrancan desde ese estado
 * del juego en lugar del reset.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cpu68k.h"
#include "despachador.h"
#include "m68k.h"
#include "maquina.h"

void programa_registrar(void);
void ref_reset(void);
void ref_despertar(void);
int ref_ciclos_base(unsigned pc);
int ref_ejecutar_una(void);
int ref_detenido(void);
unsigned ref_reg(int r);

typedef struct { int64_t ciclos; uint32_t pc, hash; } Punto;

/* Volcado de MAME: 19 registros, vectores, ventana de P2, RAM, SRAM, paleta. */
typedef struct {
    uint32_t r[19];             /* D0-D7, A0-A6, SP, USP, SR, PC */
    uint8_t vectores[128], ventana[16];
    uint8_t ram[0x10000], sram[0x10000], paleta[0x2000];
} Volcado;
static Volcado vol;
static int con_volcado;

static void cargar_volcado(const char *ruta)
{
    FILE *f = fopen(ruta, "rb");
    uint8_t magia[6];
    if (!f || fread(magia, 1, 6, f) != 6 || memcmp(magia, "PDEST1", 6)) { fprintf(stderr, "volcado inválido: %s\n", ruta); exit(2); }
    for (int i = 0; i < 19; i++) {
        uint8_t b[4];
        if (fread(b, 1, 4, f) != 4) exit(2);
        vol.r[i] = (uint32_t)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
    }
    if (fread(vol.vectores, 1, 128, f) != 128 || fread(vol.ventana, 1, 16, f) != 16 ||
        fread(vol.ram, 1, sizeof vol.ram, f) != sizeof vol.ram || fread(vol.sram, 1, sizeof vol.sram, f) != sizeof vol.sram ||
        fread(vol.paleta, 1, sizeof vol.paleta, f) != sizeof vol.paleta) { fprintf(stderr, "volcado corto\n"); exit(2); }
    fclose(f);
    con_volcado = 1;
}
static Punto *puntos;
static size_t n_puntos, cap_puntos;

static uint32_t fnv(uint32_t h, uint32_t v)
{
    for (int i = 0; i < 4; i++) { h ^= (v >> (8 * i)) & 0xFF; h *= 0x01000193u; }
    return h;
}

static uint32_t hash_regs(const uint32_t d[8], const uint32_t a[8], uint32_t sr, uint32_t usp, uint32_t ssp)
{
    uint32_t h = 0x811C9DC5u;
    for (int i = 0; i < 8; i++) { h = fnv(h, d[i]); h = fnv(h, a[i]); }
    return fnv(fnv(fnv(h, sr & 0xA71F), usp), ssp);
}

static uint32_t hash_mem(const Maquina *m)
{
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < sizeof m->ram; i++) h = (h ^ m->ram[i]) * 0x01000193u;
    for (size_t i = 0; i < sizeof m->sram; i++) h = (h ^ m->sram[i]) * 0x01000193u;
    for (size_t i = 0; i < sizeof m->paleta; i++) h = (h ^ m->paleta[i]) * 0x01000193u;
    for (size_t i = 0; i < 0x10000; i++) h = fnv(h, m->vram[i]);
    return h;
}

#define CUADRO (768 * 264)
#define LINEA_VBLANK 248
typedef struct { int64_t ciclos; uint32_t pc; } Irq;
static Irq *irqs;
static size_t n_irqs, cap_irqs;
static uint8_t *visto;            /* direcciones de instrucción ejecutadas (lado Musashi) */

/* ---------------------------------------------------------------- lado recompilado */
static int64_t limite, proxima_vblank;
static int irq_pendiente;

static void programar(const Cpu68k *c)
{
    /* Además de VBlank y el final, un punto de control cada ~1000 ciclos: con los puntos de
     * control por instrucción del código generado, así se verifica el estado con frecuencia
     * (un número primo para no sincronizarse con los bucles del programa). */
    int64_t t = c->ciclos + 997;
    if (proxima_vblank < t) t = proxima_vblank;
    if (limite < t) t = limite;
    rc_limite_ciclos = t;
    rc_irq_nivel = irq_pendiente;     /* el despachador vuelve a rc_evento si la máscara baja */
}

void rc_evento(Cpu68k *c)
{
    if (c->ciclos >= limite) { rc_salir = 1; return; }
    if (c->ciclos >= proxima_vblank) { irq_pendiente = 1; proxima_vblank += CUADRO; }
    if (c->detenido && c->ciclos < proxima_vblank && !irq_pendiente) c->ciclos = proxima_vblank;
    if (irq_pendiente && c->imask < 1) {
        if (n_irqs == cap_irqs) { cap_irqs = cap_irqs ? cap_irqs * 2 : 1024; irqs = realloc(irqs, cap_irqs * sizeof *irqs); }
        irqs[n_irqs++] = (Irq){c->ciclos, c->pc};
        uint32_t sr = cpu_get_sr(c);
        if (!c->s) { c->usp = c->a[7]; c->a[7] = c->ssp; c->s = 1; }
        c->t = 0; c->imask = 1;
        c->a[7] -= 4; bus_w32(c->a[7], c->pc);
        c->a[7] -= 2; bus_w16(c->a[7], sr);
        c->pc = bus_r32(25 * 4) & 0xFFFFFF;
        c->ciclos += 44;
        c->detenido = 0;
        irq_pendiente = 0;
    }
    programar(c);
}

/* Musashi: la misma entrada a la interrupción, hecha a mano a través de la API de registros. */
static void irq_en_musashi(void)
{
    uint32_t sr = ref_reg(M68K_REG_SR), pc = ref_reg(M68K_REG_PC);
    m68k_set_reg(M68K_REG_SR, (sr & ~0x8700u) | 0x2000u | (1u << 8));   /* S=1, T=0, I=1 */
    uint32_t sp = ref_reg(M68K_REG_A7) - 4;
    bus_w32(sp, pc);
    sp -= 2;
    bus_w16(sp, sr);
    m68k_set_reg(M68K_REG_A7, sp);
    m68k_set_reg(M68K_REG_PC, bus_r32(25 * 4));
    ref_despertar();
}

static void traza(const Cpu68k *c)
{
    if (n_puntos == cap_puntos) {
        cap_puntos = cap_puntos ? cap_puntos * 2 : 1 << 16;
        puntos = realloc(puntos, cap_puntos * sizeof *puntos);
        if (!puntos) { fprintf(stderr, "sin memoria\n"); exit(3); }
    }
    uint32_t usp = c->s ? c->usp : c->a[7], ssp = c->s ? c->a[7] : c->ssp;
    puntos[n_puntos++] = (Punto){c->ciclos, c->pc, hash_regs(c->d, c->a, cpu_get_sr(c), usp, ssp)};
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "uso: %s <mslugx_game_m68k> <mslugx_bios_m68k> [ciclos]\n", argv[0]); return 2; }
    limite = argc > 3 ? strtoll(argv[3], NULL, 10) : 20000000;
    if (argc > 4) cargar_volcado(argv[4]);
    maq_cargar_roms(argv[1], argv[2]);
    setvbuf(stdout, NULL, _IONBF, 0);

    /* ---- 1) programa recompilado */
    static Maquina mb, ma;
    static Cpu68k c;
    maq_iniciar(&mb, &c.ciclos);
    maq_usar(&mb);
    rc_interprete_iniciar();
    programa_registrar();
    memset(&c, 0, sizeof c);
    if (con_volcado) {
        maq_desde_volcado(&mb, vol.vectores, vol.ventana, vol.ram, vol.sram, vol.paleta);
        for (int i = 0; i < 8; i++) c.d[i] = vol.r[i];
        for (int i = 0; i < 7; i++) c.a[i] = vol.r[8 + i];
        uint32_t sr = vol.r[17];
        c.s = (sr >> 13) & 1; c.t = (sr >> 15) & 1; c.imask = (sr >> 8) & 7;
        cpu_set_ccr(&c, sr);
        c.a[7] = vol.r[15];
        if (c.s) c.usp = vol.r[16]; else c.ssp = 0;
        c.pc = vol.r[18] & 0xFFFFFF;
    } else {
        c.s = 1; c.imask = 7;
        c.a[7] = bus_r32(0); c.pc = bus_r32(4) & 0xFFFFFF;
    }
    proxima_vblank = (int64_t)LINEA_VBLANK * 768;
    programar(&c);
    rc_traza = traza;
    clock_t t0 = clock();
    rc_ejecutar(&c);
    double seg_b = (double)(clock() - t0) / CLOCKS_PER_SEC;
    uint32_t mem_b = hash_mem(&mb);
    printf("Recompilado: %" PRId64 " ciclos, %zu despachos (%" PRIu64 " recompilados, %" PRIu64
           " interpretados), %.2f s, pc final %06X, detenido=%d\n",
           c.ciclos, n_puntos, rc_pasos_recompilados, rc_pasos_interpretados, seg_b, c.pc, c.detenido);

    /* ---- 2) Musashi puro */
    int64_t total = 0, vista = 0;
    maq_iniciar(&ma, &vista);
    maq_usar(&ma);
    ref_reset();
    if (con_volcado) {
        maq_desde_volcado(&ma, vol.vectores, vol.ventana, vol.ram, vol.sram, vol.paleta);
        m68k_set_reg(M68K_REG_SR, vol.r[17]);
        for (int i = 0; i < 8; i++) m68k_set_reg(M68K_REG_D0 + i, vol.r[i]);
        for (int i = 0; i < 7; i++) m68k_set_reg(M68K_REG_A0 + i, vol.r[8 + i]);
        if (vol.r[17] & 0x2000) { m68k_set_reg(M68K_REG_ISP, vol.r[15]); m68k_set_reg(M68K_REG_USP, vol.r[16]); }
        else { m68k_set_reg(M68K_REG_USP, vol.r[15]); m68k_set_reg(M68K_REG_ISP, 0); }
        m68k_set_reg(M68K_REG_PC, vol.r[18]);
    }
    size_t k = 0, ki = 0;
    int ok = 1;
    visto = calloc(0x800000, 1);
    uint64_t instrucciones = 0;
    t0 = clock();
    while (k < n_puntos) {
        uint32_t pc = ref_reg(M68K_REG_PC) & 0xFFFFFF;
        if (ki < n_irqs && total == irqs[ki].ciclos && pc == irqs[ki].pc) {
            irq_en_musashi();
            total += 44;
            ki++;
            continue;
        }
        if (ref_detenido() && ki < n_irqs && total < irqs[ki].ciclos) {
            total = irqs[ki].ciclos;          /* STOP: el tiempo avanza hasta la interrupción */
            continue;
        }
        if (total == puntos[k].ciclos) {
            uint32_t d[8], a[8];
            for (int i = 0; i < 8; i++) { d[i] = ref_reg(M68K_REG_D0 + i); a[i] = ref_reg(M68K_REG_A0 + i); }
            uint32_t h = hash_regs(d, a, ref_reg(M68K_REG_SR), ref_reg(M68K_REG_USP), ref_reg(M68K_REG_ISP));
            if (pc != puntos[k].pc || h != puntos[k].hash) {
                printf("DIVERGENCIA en el punto %zu (ciclo %" PRId64 "): musashi pc=%06X recompilado pc=%06X; "
                       "registros %s\n", k, total, pc, puntos[k].pc, h == puntos[k].hash ? "iguales" : "DISTINTOS");
                if (k > 0) printf("  punto anterior correcto: %zu ciclo %" PRId64 " pc=%06X\n",
                                  k - 1, puntos[k - 1].ciclos, puntos[k - 1].pc);
                ok = 0;
                break;
            }
            k++;
            continue;
        }
        if (total > puntos[k].ciclos) {
            printf("DIVERGENCIA DE CICLOS: Musashi pasó del ciclo %" PRId64 " sin llegar al punto %zu "
                   "(pc esperado %06X; musashi en %06X, ciclo %" PRId64 ")\n",
                   puntos[k].ciclos, k, puntos[k].pc, pc, total);
            ok = 0;
            break;
        }
        if (ref_detenido()) { printf("Musashi se detuvo (STOP) antes del punto %zu\n", k); ok = 0; break; }
        vista = total + ref_ciclos_base(pc);
        visto[pc >> 1] = 1;
        total += ref_ejecutar_una();
        instrucciones++;
    }
    size_t distintas = 0, en_p1 = 0, en_bios = 0;
    for (uint32_t i = 0; i < 0x800000; i++)
        if (visto[i]) {
            distintas++;
            if (i < 0x80000) en_p1++;
            else if (i >= 0x600000 && i < 0x610000) en_bios++;
        }
    printf("Interrupciones VBlank entregadas: %zu de %zu; direcciones ejecutadas: %zu (P1 %zu, BIOS %zu)\n",
           ki, n_irqs, distintas, en_p1, en_bios);
    double seg_a = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("Musashi: %" PRIu64 " instrucciones, %.2f s; puntos verificados: %zu de %zu\n",
           instrucciones, seg_a, k, n_puntos);

    /* La última sección (desde el último punto hasta el límite) se cubre comparando la memoria
     * después de que Musashi alcance el mismo ciclo final. */
    if (ok) {
        while (total < c.ciclos) {
            uint32_t pc = ref_reg(M68K_REG_PC) & 0xFFFFFF;
            if (ki < n_irqs && total == irqs[ki].ciclos && pc == irqs[ki].pc) {
                irq_en_musashi(); total += 44; ki++; continue;
            }
            if (ref_detenido()) { total = c.ciclos; break; }
            vista = total + ref_ciclos_base(pc);
            total += ref_ejecutar_una();
        }
        uint32_t mem_a = hash_mem(&ma);
        if (total != c.ciclos) { printf("Ciclo final distinto: musashi %" PRId64 " recompilado %" PRId64 "\n", total, c.ciclos); ok = 0; }
        if (mem_a != mem_b) { printf("Memoria final DISTINTA (ram/sram/paleta/vram)\n"); ok = 0; }
        else printf("Memoria final idéntica (RAM, SRAM, paleta, VRAM)\n");
    }
    printf("%s\n", ok ? "LOCKSTEP OK" : "LOCKSTEP FALLA");
    return ok ? 0 : 1;
}
