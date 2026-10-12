/* Planificador de la máquina: líneas del LSPC, interrupciones, watchdog y el bucle del 68000.
 *
 * El 68000 recompilado corre hasta rc_limite_ciclos; ahí el despachador llama a rc_evento, que
 * procesa todos los eventos vencidos y entrega la interrupción pendiente de mayor nivel si la
 * máscara lo permite. Eventos (ciclos del 68000 desde vpos 0 del cuadro):
 *   - comienzo de cada línea (768 ciclos): dibuja la anterior, arma la lista de sprites;
 *     en la línea 256 avanza la autoanimación;
 *   - VBlank (IRQ1): línea 240 + 29 ciclos (58 ciclos del reloj maestro tras HSYNC);
 *   - recarga del contador de IRQ2 en VBlank (si está habilitada): línea 240 + 573 ciclos;
 *   - IRQ2 (contador de posición), en pasos del reloj de píxel (2 ciclos);
 *   - watchdog: ~8 cuadros sin escribir en 300001 reinician la máquina.
 * Niveles: VBlank = 1, IRQ2 = 2, IRQ3 = 3 (pendiente desde el encendido; el BIOS la reconoce).
 *
 * Entrada a la interrupción, medida contra el núcleo de ciclo exacto de MAME (Fase 4,
 * herramientas/mame/ciclos.lua + comparar_ciclos.py, 29 interrupciones):
 *   - muestreo: una IRQ activada en T entra al terminar una instrucción si T <= L, donde L es
 *     el último punto donde MAME puede detener la CPU (fin de un acceso al bus o inicio de la
 *     instrucción) antes del paso en que la instrucción muestrea las interrupciones
 *     (src/cpu/ciclos68k.h, verificado opcode por opcode y con trazas);
 *   - duración: el reconocimiento autovectorizado (VPA) ocurre 10 ciclos después del fin de la
 *     instrucción y se sincroniza con el reloj E (periodo 10, MAME m68000.cpp vpa_sync); luego
 *     faltan 35 ciclos. Total = 10 + espera E + 35 (48 a 58 ciclos, no los 44 nominales).
 *
 * Fase del cuadro: al encender, el haz está al comienzo del VBlank (vpos 240), como en MAME; lo
 * confirma la traza de E/S (herramientas/mame/traza_es.lua): con el origen en vpos 0 todas las
 * escrituras sincronizadas con el VBlank llegaban 24 líneas (1536 us) antes que en MAME. Por eso
 * el primer VBlank llega un cuadro completo después del encendido, y cada cuadro "termina" (se
 * devuelve el control al anfitrión) al comienzo del VBlank, con la imagen completa: el cuadro N
 * del port es el cuadro N de MAME (screen:frame_number()).
 */
#include "neogeo.h"

#include <stdio.h>
#include <string.h>

#include "ciclos68k.h"
#include "despachador.h"

NgMaquina ng;
FILE *ng_traza;
FILE *ng_traza_es;
int64_t ng_traza_desde, ng_traza_hasta;
FILE *ng_traza_lecturas;
void (*ng_al_reconocer_vblank)(void);
void (*ng_antes_de_leer_entradas)(void);
NgRoms ng_roms;
uint32_t ng_pc_vblank;
Cpu68k ng_cpu;

#define T_VBLANK (NG_LINEA_VBLANK * NG_CICLOS_LINEA + 29)
#define T_RECARGA (NG_LINEA_VBLANK * NG_CICLOS_LINEA + 573)
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

void ng_irq_actualizar(void)
{
    rc_irq_nivel = ng.irq3 ? 3 : ng.irq_raster ? 2 : ng.irq_vblank ? 1 : 0;
}

static void reset_cpu(void)
{
    Cpu68k *c = &ng_cpu;
    int64_t ciclos = c->ciclos;
    memset(c, 0, sizeof *c);
    c->ciclos = ciclos;
    c->s = 1;
    c->imask = 7;
    c->op = 0x4E75;                        /* clase por defecto: último acceso al final */
    c->ini = c->ciclos;
    c->a[7] = bus_r32(0);
    c->pc = bus_r32(4) & 0xFFFFFF;
    c->ciclos += ng_ciclos_reset;           /* secuencia de reset del 68000 */
}

static void reinicio_watchdog(void)
{
    ng.reinicios_watchdog++;
    fprintf(stderr, "watchdog: reinicio de la máquina (cuadro %llu, PC %06X)\n",
            (unsigned long long)ng.cuadros, ng_cpu.pc);
    ng_bus_iniciar();
    ng_sonido_iniciar();             /* el reinicio del watchdog alcanza a toda la máquina */
    ng.irq2_control = 0;
    ng.irq2_cuando = -1;
    reset_cpu();
}

void ng_encender(void)
{
    memset(&ng, 0, sizeof ng);
    memset(&ng_cpu, 0, sizeof ng_cpu);
    ng_video_iniciar();
    ng_bus_iniciar();
    ng_rtc_iniciar();
    ng_sonido_iniciar();
    ng.irq3 = 1;
    ng_irq_actualizar();
    ng.inicio_cuadro = -(int64_t)NG_LINEA_VBLANK * NG_CICLOS_LINEA;   /* t = 0 en vpos 240 */
    ng.linea = NG_LINEA_VBLANK + 1;
    vblank_hecho = recarga_hecha = 1;
    irq_desde[1] = irq_desde[2] = irq_desde[3] = 0;
    reset_cpu();
}

/* Próximo evento programado (ciclo absoluto). */
static int64_t proximo_evento(void)
{
    int64_t t = ng.inicio_cuadro + (int64_t)ng.linea * NG_CICLOS_LINEA;
    if (!vblank_hecho && ng.inicio_cuadro + T_VBLANK < t) t = ng.inicio_cuadro + T_VBLANK;
    if (!recarga_hecha && ng.inicio_cuadro + T_RECARGA < t) t = ng.inicio_cuadro + T_RECARGA;
    if (ng.irq2_cuando >= 0 && ng.irq2_cuando < t) t = ng.irq2_cuando;
    if (ng.watchdog < t) t = ng.watchdog;
    int64_t ts = ng_sonido_proximo_evento();        /* temporizadores del YM: también cortan el tramo */
    if (ts < t) t = ts;
    return t;
}

int64_t ng_proximo_limite(void) { return proximo_evento(); }

/* Procesa un evento vencido. Devuelve 1 si terminó el cuadro. */
static int procesar(int64_t ahora)
{
    if (ng.watchdog <= ahora) {
        reinicio_watchdog();
        return 0;
    }
    if (ng.irq2_cuando >= 0 && ng.irq2_cuando <= ahora) {
        int64_t t = ng.irq2_cuando;
        ng.irq2_cuando = -1;
        if (ng.irq2_control & 0x10) {
            if (!ng.irq_raster) irq_desde[2] = t;
            ng.irq_raster = 1;
            ng_irq_actualizar();
        }
        if (ng.irq2_control & 0x80)                                    /* repetición */
            ng.irq2_cuando = t + ((int64_t)ng.irq2_contador + 1) * 2;
        return 0;
    }
    if (!vblank_hecho && ng.inicio_cuadro + T_VBLANK <= ahora) {
        vblank_hecho = 1;
        if (!ng.irq_vblank) irq_desde[1] = ng.inicio_cuadro + T_VBLANK;
        ng.irq_vblank = 1;
        ng_irq_actualizar();
        return 0;
    }
    if (!recarga_hecha && ng.inicio_cuadro + T_RECARGA <= ahora) {
        recarga_hecha = 1;
        if (ng.irq2_control & 0x40)
            ng.irq2_cuando = ng.inicio_cuadro + T_RECARGA + ((int64_t)ng.irq2_contador + 1) * 2;
        return 0;
    }
    /* comienzo de línea (si no es la hora de la línea, el evento era un temporizador del YM2610,
       que ya atendió ng_sonido_hasta) */
    if (ng.inicio_cuadro + (int64_t)ng.linea * NG_CICLOS_LINEA > ahora) return 0;
    if (ng.linea == NG_LINEAS) {
        ng.inicio_cuadro += NG_CICLOS_CUADRO;
        ng.linea = 0;
        vblank_hecho = recarga_hecha = 0;
    }
    ng_video_inicio_linea(ng.linea);
    if (ng.linea == 0x100) ng_video_anim_tick();
    ng.linea++;
    if (ng.linea - 1 == NG_LINEA_VBLANK) {        /* imagen completa: fin del cuadro */
        ng.cuadros++;
        return 1;
    }
    return 0;
}

void ng_al_dia(int64_t t)
{
    for (;;) {
        int64_t e = proximo_evento();
        if (e > t) break;
        ng_sonido_hasta(e);
        if (procesar(e)) rc_salir = 1;
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
    c->op = 0x4E75; c->ini = c->ciclos;     /* la entrada termina con accesos al bus */
    c->ciclos += CIC(ciclos_entrada_irq(c->ciclos));
    c->detenido = 0;
}

void rc_evento(Cpu68k *c)
{
    for (;;) {
        int64_t t = proximo_evento();
        if (c->ciclos >= t) {
            /* Como el planificador de MAME: el Z80 corre hasta el límite del tramo. */
            ng_sonido_hasta(t);
            if (procesar(t)) { rc_salir = 1; break; }
            continue;
        }
        int visible = 0;
        for (int n = 3; n >= 1 && !visible; n--) {
            int pend = n == 3 ? ng.irq3 : n == 2 ? ng.irq_raster : ng.irq_vblank;
            if (pend && irq_desde[n] <= ciclos_acceso_l(c)) visible = n;
        }
        if (visible > c->imask) {
            entregar_irq(c, visible);
            continue;                       /* la entrada consume ciclos: puede vencer otro evento */
        }
        if (c->detenido) {                  /* STOP: espera al próximo evento o a la IRQ */
            int64_t d = t;
            for (int n = 1; n <= 3; n++) {
                int pend = n == 3 ? ng.irq3 : n == 2 ? ng.irq_raster : ng.irq_vblank;
                if (pend && n > c->imask && irq_desde[n] + MUESTREO_MINIMO < d) d = irq_desde[n] + MUESTREO_MINIMO;
            }
            c->ciclos = d > c->ciclos ? d : c->ciclos + 1;
            continue;
        }
        break;
    }
    /* Límite de ejecución: el próximo evento o el primer momento en que una IRQ pendiente y no
       enmascarada podría atenderse (MUESTREO_MINIMO ciclos después de activarse). */
    int64_t limite = proximo_evento();
    for (int n = 1; n <= 3; n++) {
        int pend = n == 3 ? ng.irq3 : n == 2 ? ng.irq_raster : ng.irq_vblank;
        if (pend && n > c->imask && irq_desde[n] + MUESTREO_MINIMO < limite) limite = irq_desde[n] + MUESTREO_MINIMO;
    }
    rc_limite_ciclos = limite;
}

void ng_maquina_estado(NgEstado *e)
{
    ng_estado_bloque(e, &ng, sizeof ng);
    ng_estado_bloque(e, &ng_cpu, sizeof ng_cpu);
    ng_estado_bloque(e, irq_desde, sizeof irq_desde);
    ng_estado_bloque(e, &vblank_hecho, sizeof vblank_hecho);
    ng_estado_bloque(e, &recarga_hecha, sizeof recarga_hecha);
    if (!e->guardando) ng_irq_actualizar();
}

void ng_ejecutar_cuadro(void)
{
    rc_salir = 0;
    rc_limite_ciclos = 0;                   /* procesa de inmediato lo que esté vencido */
    rc_ejecutar(&ng_cpu);
}
