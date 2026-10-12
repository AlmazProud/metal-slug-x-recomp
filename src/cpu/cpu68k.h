/* State del 68000 y semántica de las operaciones para el código recompilado.
 *
 * El código C que genera tools/recomp68k/ usa exclusivamente estas definiciones:
 * registros en Cpu68k, accesos a memoria a través de las funciones de bus, y una función
 * inline por operación que calcula el resultado y los flags del CCR.
 *
 * La semántica se valida instrucción por instrucción contra Musashi (tests/differential/).
 * Referencias de comportamiento: Motorola M68000 Programmer's Reference Manual.
 */
#ifndef CPU68K_H
#define CPU68K_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Cpu68k {
    uint32_t d[8];
    uint32_t a[8];          /* a[7] es el puntero de pila active (SSP o USP según S) */
    uint32_t pc;
    uint32_t usp, ssp;      /* copia del puntero inactivo */
    /* CCR desglosado: cada flag vale 0 o 1 */
    uint8_t x, n, z, v, c;
    /* resto del SR: mode supervisor, traza y máscara de interrupciones */
    uint8_t s, t, imask;
    int64_t cycles;         /* cycles de reloj del 68000 consumidos */
    uint8_t stopped;       /* STOP ejecutado: espera una interrupción */
    /* Muestreo de interrupciones (cycles68k.h): opcode y ciclo de inicio de la última
     * instrucción ejecutada. */
    uint16_t op;
    int64_t start_cycle;
} Cpu68k;

/* ------------------------------------------------------------------ bus */
/* Las implementa el runtime (o el arnés de tests). Direcciones de 24 bits. */
uint32_t bus_r8(uint32_t a);
uint32_t bus_r16(uint32_t a);
void bus_w8(uint32_t a, uint32_t v);
void bus_w16(uint32_t a, uint32_t v);

static inline uint32_t bus_r32(uint32_t a) { return bus_r16(a) << 16 | bus_r16(a + 2); }
static inline void bus_w32(uint32_t a, uint32_t v) { bus_w16(a, v >> 16); bus_w16(a + 2, v & 0xFFFF); }

/* ------------------------------------------------------------------ SR */
static inline uint32_t cpu_get_sr(const Cpu68k *c)
{
    return (uint32_t)c->t << 15 | (uint32_t)c->s << 13 | (uint32_t)c->imask << 8 |
           (uint32_t)c->x << 4 | (uint32_t)c->n << 3 | (uint32_t)c->z << 2 | (uint32_t)c->v << 1 | c->c;
}

static inline void cpu_set_ccr(Cpu68k *c, uint32_t ccr)
{
    c->x = ccr >> 4 & 1; c->n = ccr >> 3 & 1; c->z = ccr >> 2 & 1; c->v = ccr >> 1 & 1; c->c = ccr & 1;
}

static inline void cpu_set_sr(Cpu68k *c, uint32_t sr)
{
    uint8_t s_nuevo = sr >> 13 & 1;
    if (s_nuevo != c->s) {                     /* cambio de mode: intercambia punteros de pila */
        if (s_nuevo) { c->usp = c->a[7]; c->a[7] = c->ssp; }
        else         { c->ssp = c->a[7]; c->a[7] = c->usp; }
        c->s = s_nuevo;
    }
    c->t = sr >> 15 & 1;
    c->imask = sr >> 8 & 7;
    cpu_set_ccr(c, sr);
}

/* ------------------------------------------------------------------ excepciones */
/* Excepción de grupo 1/2 del 68000 (TRAP, división por cero, CHK, TRAPV, privilegio, ilegal):
 * pasa a supervisor, apila PC y SR (6 bytes) y salta al vector. */
static inline void cpu_exception(Cpu68k *c, int vector, uint32_t return_pc)
{
    uint32_t sr = cpu_get_sr(c);
    if (!c->s) { c->usp = c->a[7]; c->a[7] = c->ssp; c->s = 1; }
    c->t = 0;
    c->a[7] -= 4; bus_w32(c->a[7], return_pc);
    c->a[7] -= 2; bus_w16(c->a[7], sr);
    c->pc = bus_r32((uint32_t)vector * 4);
}

/* ------------------------------------------------------------------ tamaños */
#define MASK_B 0xFFu
#define MASK_W 0xFFFFu
#define MASK_L 0xFFFFFFFFu
#define MSB_B 0x80u
#define MSB_W 0x8000u
#define MSB_L 0x80000000u

static inline uint32_t sext8(uint32_t v)  { return (uint32_t)(int32_t)(int8_t)v; }
static inline uint32_t sext16(uint32_t v) { return (uint32_t)(int32_t)(int16_t)v; }

/* Escribe en un registro de data solo los bits del tamaño de la operación. */
static inline void set_d(Cpu68k *c, int r, uint32_t v, uint32_t mask) { c->d[r] = (c->d[r] & ~mask) | (v & mask); }

/* N y Z de un resultado del tamaño dado. */
static inline void flags_nz(Cpu68k *c, uint32_t r, uint32_t mask, uint32_t msb)
{
    c->n = (r & msb) != 0;
    c->z = (r & mask) == 0;
}

/* ------------------------------------------------------------------ operaciones */
/* Cada operación recibe los operandos ya leídos, devuelve el resultado (sin escribirlo)
 * y actualiza los flags exactamente como el 68000. */

/* MOVE, MOVEQ, TST, CLR, AND/OR/EOR, NOT: N y Z según el resultado; V = C = 0; X intacto. */
static inline uint32_t op_logic(Cpu68k *c, uint32_t r, uint32_t mask, uint32_t msb)
{
    flags_nz(c, r, mask, msb);
    c->v = 0; c->c = 0;
    return r & mask;
}

/* ADD / ADDI / ADDQ (no a registro de dirección). */
static inline uint32_t op_add(Cpu68k *c, uint32_t s, uint32_t d, uint32_t mask, uint32_t msb)
{
    uint32_t r = (d + s) & mask;
    s &= mask; d &= mask;
    flags_nz(c, r, mask, msb);
    c->v = ((s ^ r) & (d ^ r) & msb) != 0;
    c->c = c->x = (((s & d) | (~r & (s | d))) & msb) != 0;
    return r;
}

/* SUB / SUBI / SUBQ: d - s. */
static inline uint32_t op_sub(Cpu68k *c, uint32_t s, uint32_t d, uint32_t mask, uint32_t msb)
{
    uint32_t r = (d - s) & mask;
    s &= mask; d &= mask;
    flags_nz(c, r, mask, msb);
    c->v = ((s ^ d) & (r ^ d) & msb) != 0;
    c->c = c->x = (((s & ~d) | (r & ~d) | (s & r)) & msb) != 0;
    return r;
}

/* CMP / CMPI / CMPA: como SUB pero sin resultado ni X. */
static inline void op_cmp(Cpu68k *c, uint32_t s, uint32_t d, uint32_t mask, uint32_t msb)
{
    uint32_t r = (d - s) & mask;
    s &= mask; d &= mask;
    flags_nz(c, r, mask, msb);
    c->v = ((s ^ d) & (r ^ d) & msb) != 0;
    c->c = (((s & ~d) | (r & ~d) | (s & r)) & msb) != 0;
}

/* ADDX: d + s + X. Z solo se borra si el resultado no es cero (no se active). */
static inline uint32_t op_addx(Cpu68k *c, uint32_t s, uint32_t d, uint32_t mask, uint32_t msb)
{
    uint32_t r = (d + s + c->x) & mask;
    s &= mask; d &= mask;
    c->n = (r & msb) != 0;
    if (r) c->z = 0;
    c->v = ((s ^ r) & (d ^ r) & msb) != 0;
    c->c = c->x = (((s & d) | (~r & (s | d))) & msb) != 0;
    return r;
}

/* SUBX: d - s - X. */
static inline uint32_t op_subx(Cpu68k *c, uint32_t s, uint32_t d, uint32_t mask, uint32_t msb)
{
    uint32_t r = (d - s - c->x) & mask;
    s &= mask; d &= mask;
    c->n = (r & msb) != 0;
    if (r) c->z = 0;
    c->v = ((s ^ d) & (r ^ d) & msb) != 0;
    c->c = c->x = (((s & ~d) | (r & ~d) | (s & r)) & msb) != 0;
    return r;
}

/* NEG: 0 - d. */
static inline uint32_t op_neg(Cpu68k *c, uint32_t d, uint32_t mask, uint32_t msb)
{
    return op_sub(c, d, 0, mask, msb);
}

/* NEGX: 0 - d - X. */
static inline uint32_t op_negx(Cpu68k *c, uint32_t d, uint32_t mask, uint32_t msb)
{
    return op_subx(c, d, 0, mask, msb);
}

/* ------------------------------------------------------------------ desplazamientos */
enum { SH_AS, SH_LS, SH_ROX, SH_RO };

/* ASx/LSx/ROXx/ROx de `cnt` posiciones (0-63), bit a bit: exacto por definición.
 * cnt = 0: C = 0 (ROX: C = X), X intacto, V = 0, N y Z según el operando. */
static inline uint32_t op_shift(Cpu68k *c, int tipo, int izq, uint32_t cnt, uint32_t v,
                                uint32_t mask, uint32_t msb)
{
    v &= mask;
    uint32_t sale = 0;
    int vflag = 0;
    for (uint32_t k = 0; k < cnt; k++) {
        if (izq) {
            sale = v & msb;
            uint32_t entra = tipo == SH_RO ? (sale != 0) : tipo == SH_ROX ? c->x : 0;
            uint32_t nv = ((v << 1) | entra) & mask;
            if (tipo == SH_AS && ((nv ^ v) & msb)) vflag = 1;   /* el MSB cambió en algún paso */
            v = nv;
        } else {
            sale = v & 1;
            uint32_t entra = tipo == SH_AS ? (v & msb)
                           : tipo == SH_RO ? (sale ? msb : 0)
                           : tipo == SH_ROX ? (c->x ? msb : 0) : 0;
            v = (v >> 1) | entra;
        }
        if (tipo == SH_ROX) c->x = sale != 0;
    }
    if (cnt == 0) {
        c->c = tipo == SH_ROX ? c->x : 0;
    } else {
        c->c = sale != 0;
        if (tipo == SH_AS || tipo == SH_LS) c->x = c->c;
    }
    c->v = (uint8_t)vflag;
    flags_nz(c, v, mask, msb);
    return v;
}

/* ------------------------------------------------------------------ cycles de MUL */
/* Ciclos extra del 68000 en MULU/MULS (el costo base ya está en la tabla por opcode):
 * MULU suma 2 por cada bit en 1 del operando fuente; MULS, 2 por cada transición 0->1 o 1->0
 * recorriendo el operando con signo extendido. Igual que Musashi 4.60 para el 68000. */
static inline int cycles_mulu(uint32_t s)
{
    int n = 0;
    for (uint32_t y = s & 0xFFFF; y; y >>= 1) n += (y & 1) * 2;
    return n;
}

/* MULS: 2 cycles por cada par 01/10 en la fuente de 17 bits (la palabra con un 0 añadido como
 * bit menos significativo), según el manual del 68000; verificado contra MAME (Fase 4). */
static inline int cycles_muls(uint32_t s)
{
    uint32_t t = ((s << 1) ^ s) & 0xFFFF;
    int n = 0;
    for (; t; t &= t - 1) n += 2;
    return n;
}

/* ------------------------------------------------------------------ división */
/* DIVU.W: d[r] (32 bits) / s (16 bits, no cero). Desbordamiento: solo V = 1 y el registro
 * no cambia (N y Z quedan como estaban: indefinidos en el manual; así lo hace Musashi,
 * pendiente de confirmar contra MAME). C = 0 si no desborda. */
static inline void op_divu(Cpu68k *c, int r, uint32_t s)
{
    uint32_t q = c->d[r] / s, resto = c->d[r] % s;
    if (q > 0xFFFF) { c->v = 1; return; }
    c->d[r] = (q & 0xFFFF) | resto << 16;
    c->n = (q & 0x8000) != 0; c->z = q == 0; c->v = 0; c->c = 0;
}

/* DIVS.W con signo. 0x80000000 / -1 desborda (V = 1) como en el hardware. */
static inline void op_divs(Cpu68k *c, int r, uint32_t s)
{
    int32_t dividendo = (int32_t)c->d[r], divisor = (int16_t)s;
    if (dividendo == INT32_MIN && divisor == -1) { c->v = 1; return; }
    int32_t q = dividendo / divisor, resto = dividendo % divisor;
    if (q < -32768 || q > 32767) { c->v = 1; return; }
    c->d[r] = ((uint32_t)q & 0xFFFF) | (uint32_t)resto << 16;
    c->n = (q & 0x8000) != 0; c->z = (q & 0xFFFF) == 0; c->v = 0; c->c = 0;
}

/* ------------------------------------------------------------------ BCD */
/* ABCD / SBCD: resultado, C, X y Z según el manual (Z solo se borra). N y V son indefinidos en
 * el manual; se reproducen las fórmulas de Musashi 4.60 (MIT, Karl Stenerud) hasta confirmarlas
 * contra MAME. */
static inline uint32_t op_abcd(Cpu68k *c, uint32_t s, uint32_t d)
{
    uint32_t res = (s & 0x0F) + (d & 0x0F) + c->x;
    uint32_t v = ~res;
    if (res > 9) res += 6;
    res += (s & 0xF0) + (d & 0xF0);
    c->c = c->x = res > 0x99;
    if (c->c) res -= 0xA0;
    c->v = ((v & res) & 0x80) != 0;
    c->n = (res & 0x80) != 0;
    res &= 0xFF;
    if (res) c->z = 0;
    return res;
}

static inline uint32_t op_sbcd(Cpu68k *c, uint32_t s, uint32_t d)
{
    uint32_t res = (d & 0x0F) - (s & 0x0F) - c->x;
    uint32_t v = ~res;
    if (res > 9) res -= 6;
    res += (d & 0xF0) - (s & 0xF0);
    c->c = c->x = res > 0x99;
    if (c->c) res += 0xA0;
    res &= 0xFF;
    c->v = ((v & res) & 0x80) != 0;
    c->n = (res & 0x80) != 0;
    if (res) c->z = 0;
    return res;
}

/* ------------------------------------------------------------------ condiciones */
/* Códigos de condición del 68000 (Bcc, Scc, DBcc). */
enum { CC_T, CC_F, CC_HI, CC_LS, CC_CC, CC_CS, CC_NE, CC_EQ,
       CC_VC, CC_VS, CC_PL, CC_MI, CC_GE, CC_LT, CC_GT, CC_LE };

static inline bool cond(const Cpu68k *c, int cc)
{
    switch (cc) {
    case CC_T:  return true;
    case CC_F:  return false;
    case CC_HI: return !c->c && !c->z;
    case CC_LS: return c->c || c->z;
    case CC_CC: return !c->c;
    case CC_CS: return c->c;
    case CC_NE: return !c->z;
    case CC_EQ: return c->z;
    case CC_VC: return !c->v;
    case CC_VS: return c->v;
    case CC_PL: return !c->n;
    case CC_MI: return c->n;
    case CC_GE: return c->n == c->v;
    case CC_LT: return c->n != c->v;
    case CC_GT: return !c->z && c->n == c->v;
    default:    return c->z || c->n != c->v;   /* CC_LE */
    }
}

#endif
