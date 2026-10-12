/* Estado del 68000 y semántica de las operaciones para el código recompilado.
 *
 * El código C que genera herramientas/recomp68k/ usa exclusivamente estas definiciones:
 * registros en Cpu68k, accesos a memoria a través de las funciones de bus, y una función
 * inline por operación que calcula el resultado y los flags del CCR.
 *
 * La semántica se valida instrucción por instrucción contra Musashi (tests/diferencial/).
 * Referencias de comportamiento: Motorola M68000 Programmer's Reference Manual.
 */
#ifndef CPU68K_H
#define CPU68K_H

#include <stdbool.h>
#include <stdint.h>

typedef struct Cpu68k {
    uint32_t d[8];
    uint32_t a[8];          /* a[7] es el puntero de pila activo (SSP o USP según S) */
    uint32_t pc;
    uint32_t usp, ssp;      /* copia del puntero inactivo */
    /* CCR desglosado: cada flag vale 0 o 1 */
    uint8_t x, n, z, v, c;
    /* resto del SR: modo supervisor, traza y máscara de interrupciones */
    uint8_t s, t, imask;
    int64_t ciclos;         /* ciclos de reloj del 68000 consumidos */
} Cpu68k;

/* ------------------------------------------------------------------ bus */
/* Las implementa el runtime (o el arnés de pruebas). Direcciones de 24 bits. */
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
    if (s_nuevo != c->s) {                     /* cambio de modo: intercambia punteros de pila */
        if (s_nuevo) { c->usp = c->a[7]; c->a[7] = c->ssp; }
        else         { c->ssp = c->a[7]; c->a[7] = c->usp; }
        c->s = s_nuevo;
    }
    c->t = sr >> 15 & 1;
    c->imask = sr >> 8 & 7;
    cpu_set_ccr(c, sr);
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

/* Escribe en un registro de datos solo los bits del tamaño de la operación. */
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

/* ADDX: d + s + X. Z solo se borra si el resultado no es cero (no se activa). */
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
