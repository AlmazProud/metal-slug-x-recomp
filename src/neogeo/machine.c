/* Planificador de la máquina: líneas del LSPC, interrupciones, watchdog y el bucle del 68000.
 *
 * El 68000 recompilado corre hasta rc_cycle_limit; ahí el despachador llama a rc_event, que
 * procesa todos los eventos vencidos y entrega la interrupción pendiente de mayor nivel si la
 * máscara lo permite. Eventos (cycles del 68000 desde vpos 0 del cuadro):
 *   - comienzo de cada línea (768 cycles): dibuja la anterior, arma la lista de sprites;
 *     en la línea 256 avanza la autoanimación;
 *   - VBlank (IRQ1): línea 240 + 29 cycles (58 cycles del reloj maestro tras HSYNC);
 *   - recarga del contador de IRQ2 en VBlank (si está habilitada): línea 240 + 573 cycles;
 *   - IRQ2 (contador de posición), en pasos del reloj de píxel (2 cycles);
 *   - watchdog: ~8 frames sin escribir en 300001 reinician la máquina.
 * Niveles: VBlank = 1, IRQ2 = 2, IRQ3 = 3 (pendiente desde el encendido; el BIOS la reconoce).
 *
 * Entrada a la interrupción, medida contra el núcleo de ciclo exacto de MAME (Fase 4,
 * tools/mame/cycles.lua + compare_cycles.py, 29 interrupciones):
 *   - muestreo: una IRQ activada en T entra al terminar una instrucción si T <= L, donde L es
 *     el último punto donde MAME puede detener la CPU (fin de un acceso al bus o inicio de la
 *     instrucción) antes del paso en que la instrucción muestrea las interrupciones
 *     (src/cpu/cycles68k.h, verificado opcode por opcode y con trazas);
 *   - duración: el reconocimiento autovectorizado (VPA) ocurre 10 cycles después del fin de la
 *     instrucción y se sincroniza con el reloj E (periodo 10, MAME m68000.cpp vpa_sync); luego
 *     faltan 35 cycles. Total = 10 + espera E + 35 (48 a 58 cycles, no los 44 nominales).
 *
 * Fase del cuadro: al encender, el haz está al comienzo del VBlank (vpos 240), como en MAME; lo
 * confirma la traza de E/S (tools/mame/trace_io.lua): con el origen en vpos 0 todas las
 * escrituras sincronizadas con el VBlank llegaban 24 líneas (1536 us) antes que en MAME. Por eso
 * el primer VBlank llega un cuadro completo después del encendido, y cada cuadro "termina" (se
 * devuelve el control al anfitrión) al comienzo del VBlank, con la imagen completa: el cuadro N
 * del port es el cuadro N de MAME (screen:frame_number()).
 */
#include "neogeo.h"

#include <stdio.h>
#include <string.h>

#include "cycles68k.h"
#include "dispatcher.h"

NgMachine ng;
FILE *ng_trace;
FILE *ng_trace_io;
int64_t ng_trace_from, ng_trace_to;
FILE *ng_trace_reads;
void (*ng_on_vblank_ack)(void);
void (*ng_before_input_read)(void);
NgRoms ng_roms;
uint32_t ng_pc_vblank;
NgDebug ng_dbg;
NgForcedRead ng_forced_read[NG_FORCED_COUNT];
Cpu68k ng_cpu;

#define T_VBLANK (NG_VBLANK_LINE * NG_CYCLES_PER_LINE + 29)
#define T_RECARGA (NG_VBLANK_LINE * NG_CYCLES_PER_LINE + 573)
#define MUESTREO_MINIMO 0          /* L <= fin: ninguna IRQ entra antes de T */

static int64_t irq_desde[4];               /* ciclo en que se activó cada nivel */

static int vblank_hecho, recarga_hecha;
int ng_ciclos_reset = 34;              /* MAME: la primera instrucción empieza en el ciclo 34 */

/* Ciclos de la entrada a una interrupción que empieza al terminar la instrucción en s. */
static int ciclos_entrada_irq(int64_t s)
{
    int64_t t = s + 10;                         /* ciclo de reconocimiento (IACK) */
    int m = (int)(t % 10);
    int espera = m < 7 ? 10 - m : 20 - m;       /* hasta el siguiente flanco útil del reloj E */
    return 10 + espera + 35;
}

void ng_irq_update(void)
{
    rc_irq_level = ng.irq3 ? 3 : ng.irq_raster ? 2 : ng.irq_vblank ? 1 : 0;
}

static void reset_cpu(void)
{
    Cpu68k *c = &ng_cpu;
    int64_t cycles = c->cycles;
    memset(c, 0, sizeof *c);
    c->cycles = cycles;
    c->s = 1;
    c->imask = 7;
    c->op = 0x4E75;                        /* clase por defecto: último acceso al final */
    c->start_cycle = c->cycles;
    c->a[7] = bus_r32(0);
    c->pc = bus_r32(4) & 0xFFFFFF;
    c->cycles += ng_ciclos_reset;           /* secuencia de reset del 68000 */
}

static void reinicio_watchdog(void)
{
    ng.watchdog_resets++;
    fprintf(stderr, "watchdog: reinicio de la máquina (cuadro %llu, PC %06X)\n",
            (unsigned long long)ng.frames, ng_cpu.pc);
    ng_bus_init();
    ng_sound_init();             /* el reinicio del watchdog alcanza a toda la máquina */
    ng.irq2_control = 0;
    ng.irq2_when = -1;
    reset_cpu();
}

void ng_power_on(void)
{
    memset(&ng, 0, sizeof ng);
    memset(&ng_cpu, 0, sizeof ng_cpu);
    ng_video_init();
    ng_bus_init();
    ng_rtc_init();
    ng_sound_init();
    ng.irq3 = 1;
    ng_irq_update();
    ng.frame_start = -(int64_t)NG_VBLANK_LINE * NG_CYCLES_PER_LINE;   /* t = 0 en vpos 240 */
    ng.line = NG_VBLANK_LINE + 1;
    vblank_hecho = recarga_hecha = 1;
    irq_desde[1] = irq_desde[2] = irq_desde[3] = 0;
    reset_cpu();
}

/* Próximo evento programado (ciclo absoluto). */
static int64_t proximo_evento(void)
{
    int64_t t = ng.frame_start + (int64_t)ng.line * NG_CYCLES_PER_LINE;
    if (!vblank_hecho && ng.frame_start + T_VBLANK < t) t = ng.frame_start + T_VBLANK;
    if (!recarga_hecha && ng.frame_start + T_RECARGA < t) t = ng.frame_start + T_RECARGA;
    if (ng.irq2_when >= 0 && ng.irq2_when < t) t = ng.irq2_when;
    if (ng.watchdog < t) t = ng.watchdog;
    int64_t ts = ng_sound_next_event();        /* temporizadores del YM: también cortan el tramo */
    if (ts < t) t = ts;
    return t;
}

int64_t ng_next_limit(void) { return proximo_evento(); }

/* Procesa un evento vencido. Devuelve 1 si terminó el cuadro. */
static int procesar(int64_t ahora)
{
    if (ng.watchdog <= ahora) {
        reinicio_watchdog();
        return 0;
    }
    if (ng.irq2_when >= 0 && ng.irq2_when <= ahora) {
        int64_t t = ng.irq2_when;
        ng.irq2_when = -1;
        if (ng.irq2_control & 0x10) {
            if (!ng.irq_raster) irq_desde[2] = t;
            ng.irq_raster = 1;
            ng_irq_update();
        }
        if (ng.irq2_control & 0x80)                                    /* repetición */
            ng.irq2_when = t + ((int64_t)ng.irq2_counter + 1) * 2;
        return 0;
    }
    if (!vblank_hecho && ng.frame_start + T_VBLANK <= ahora) {
        vblank_hecho = 1;
        if (!ng.irq_vblank) irq_desde[1] = ng.frame_start + T_VBLANK;
        ng.irq_vblank = 1;
        ng_irq_update();
        return 0;
    }
    if (!recarga_hecha && ng.frame_start + T_RECARGA <= ahora) {
        recarga_hecha = 1;
        if (ng.irq2_control & 0x40)
            ng.irq2_when = ng.frame_start + T_RECARGA + ((int64_t)ng.irq2_counter + 1) * 2;
        return 0;
    }
    /* comienzo de línea (si no es la hour de la línea, el evento era un temporizador del YM2610,
       que ya atendió ng_sound_until) */
    if (ng.frame_start + (int64_t)ng.line * NG_CYCLES_PER_LINE > ahora) return 0;
    if (ng.line == NG_LINES) {
        ng.frame_start += NG_CYCLES_PER_FRAME;
        ng.line = 0;
        vblank_hecho = recarga_hecha = 0;
    }
    if (ng_dbg.active && ng_cpu.pc >= ng_dbg.wait_start && ng_cpu.pc < ng_dbg.wait_end)
        ng_dbg.wait_lines_current++;
    ng_video_line_start(ng.line);
    if (ng.line == 0x100) ng_video_anim_tick();
    ng.line++;
    if (ng.line - 1 == NG_VBLANK_LINE) {        /* imagen completa: fin del cuadro */
        ng.frames++;
        if (ng_dbg.active) {
            ng_dbg.wait_lines = ng_dbg.wait_lines_current;
            ng_dbg.wait_lines_current = 0;
            memcpy(ng_dbg.box, ng_dbg.box_current, sizeof ng_dbg.box);
            for (int i = 0; i < 381; i++) ng_dbg.box_current[i] = (NgBox){1, 0, 1, 0};
        }
        return 1;
    }
    return 0;
}

void ng_catch_up(int64_t t)
{
    for (;;) {
        int64_t e = proximo_evento();
        if (e > t) break;
        ng_sound_until(e);
        if (procesar(e)) rc_exit = 1;
    }
}

static void entregar_irq(Cpu68k *c, int nivel)
{
    if (nivel == 1) ng_pc_vblank = c->pc;
    uint32_t sr = cpu_get_sr(c);
    if (!c->s) { c->usp = c->a[7]; c->a[7] = c->ssp; c->s = 1; }
    c->t = 0;
    c->imask = (uint8_t)nivel;
    c->a[7] -= 4; bus_w32(c->a[7], c->pc);
    c->a[7] -= 2; bus_w16(c->a[7], sr);
    c->pc = bus_r32((uint32_t)(24 + nivel) * 4) & 0xFFFFFF;
    c->op = 0x4E75; c->start_cycle = c->cycles;     /* la entrada termina con accesos al bus */
    c->cycles += CIC(ciclos_entrada_irq(c->cycles));
    c->stopped = 0;
}

void rc_event(Cpu68k *c)
{
    for (;;) {
        int64_t t = proximo_evento();
        if (c->cycles >= t) {
            /* Como el planificador de MAME: el Z80 corre hasta el límite del tramo. */
            ng_sound_until(t);
            if (procesar(t)) { rc_exit = 1; break; }
            continue;
        }
        int visible = 0;
        for (int n = 3; n >= 1 && !visible; n--) {
            int pend = n == 3 ? ng.irq3 : n == 2 ? ng.irq_raster : ng.irq_vblank;
            if (pend && irq_desde[n] <= cycles_access_l(c)) visible = n;
        }
        if (visible > c->imask) {
            entregar_irq(c, visible);
            continue;                       /* la entrada consume cycles: puede vencer otro evento */
        }
        if (c->stopped) {                  /* STOP: espera al próximo evento o a la IRQ */
            int64_t d = t;
            for (int n = 1; n <= 3; n++) {
                int pend = n == 3 ? ng.irq3 : n == 2 ? ng.irq_raster : ng.irq_vblank;
                if (pend && n > c->imask && irq_desde[n] + MUESTREO_MINIMO < d) d = irq_desde[n] + MUESTREO_MINIMO;
            }
            c->cycles = d > c->cycles ? d : c->cycles + 1;
            continue;
        }
        break;
    }
    /* Límite de ejecución: el próximo evento o el primer momento en que una IRQ pendiente y no
       enmascarada podría atenderse (MUESTREO_MINIMO cycles después de activarse). */
    int64_t limite = proximo_evento();
    for (int n = 1; n <= 3; n++) {
        int pend = n == 3 ? ng.irq3 : n == 2 ? ng.irq_raster : ng.irq_vblank;
        if (pend && n > c->imask && irq_desde[n] + MUESTREO_MINIMO < limite) limite = irq_desde[n] + MUESTREO_MINIMO;
    }
    rc_cycle_limit = limite;
}

void ng_machine_state(NgState *e)
{
    ng_state_block(e, &ng, sizeof ng);
    ng_state_block(e, &ng_cpu, sizeof ng_cpu);
    ng_state_block(e, irq_desde, sizeof irq_desde);
    ng_state_block(e, &vblank_hecho, sizeof vblank_hecho);
    ng_state_block(e, &recarga_hecha, sizeof recarga_hecha);
    if (!e->saving) ng_irq_update();
}

void ng_run_frame(void)
{
    rc_exit = 0;
    rc_cycle_limit = 0;                   /* procesa de inmediato lo que esté vencido */
    rc_run(&ng_cpu);
}
