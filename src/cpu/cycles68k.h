/* Ajustes a la tabla de cycles de Musashi para que coincida con el 68000 real (MAME, el oráculo).
 *
 * La tabla base sigue siendo la de Musashi (volcada al compilar); estas funciones devuelven la
 * diferencia a sumar. Cada regla se verificó midiendo en MAME (tools/mame/cycles.lua y
 * tools/audit_cycles.py, docs/PHASE4_VERIFICATION.md). Las usan el código generado y el
 * intérprete de respaldo, con el estado del 68000 ANTERIOR a la instrucción, así ambos caminos
 * siguen dando los mismos cycles.
 */
#ifndef CYCLES68K_H
#define CYCLES68K_H

#include <stdint.h>
#include <stdlib.h>

#include "cpu68k.h"

/* Costo en cycles de una instrucción, con el "turbo" opcional de la Fase 5: con cpu_turbo = k
 * cada instrucción cuesta 2^k veces menos, como un 68000 más rápido frente al mismo hardware
 * (quita la ralentización original). Con 0 (por defecto) no cambia nada. */
#ifndef CIC
extern int cpu_turbo;
#define CIC(n) ((n) >> cpu_turbo)
#endif

/* Ajuste por opcode (y su primera palabra de extensión) que no depende de la división. */
static inline int cycles_adjust(const Cpu68k *c, uint32_t op, uint32_t ext)
{
    uint32_t mode = (op >> 3) & 7, reg = op & 7;
    /* ADDA.L / SUBA.L Dn,An y An,An: 8, no 6 (MAME: adda.l D0,A2 = 8). */
    if (((op & 0xF1C0) == 0xD1C0 || (op & 0xF1C0) == 0x91C0) && mode <= 1) return 2;
    /* ADD/SUB/AND/OR.L <ea>,Dn con fuente Dn o An: 8, no 6 (manual del 68000, nota "**";
       MAME: add.l D4,D7 = 8). */
    if (((op & 0xF1C0) == 0xD080 || (op & 0xF1C0) == 0x9080 || (op & 0xF1C0) == 0xC080 ||
         (op & 0xF1C0) == 0x8080) && mode <= 1) return 2;
    /* ADDA.W / SUBA.W #imm,An: 12, no 14 (MAME: adda.w #$28,A0 y suba.w #$28,A0 = 12). */
    if (((op & 0xF1FF) == 0xD0FC || (op & 0xF1FF) == 0x90FC)) return -2;
    /* ANDI.L #imm,Dn: 16, no 14 (MAME: andi.l #$ffff,D1 = 16). */
    if ((op & 0xFFF8) == 0x0280) return 2;
    /* ADDQ.W #n,An: 8, no 4 (MAME: addq.w #1,A0 = 8; SUBQ.W ya era 8). */
    if ((op & 0xF1F8) == 0x5048) return 4;
    /* BCHG/BCLR/BSET sobre Dn con número de bit < 16: 2 cycles menos que el máximo de la tabla
       (MAME: bset #7,D0 = 10; bclr #2,D1 = 12; bset D0,D7 = 6). BTST no cambia. */
    if (((op & 0xFFF8) == 0x0840 || (op & 0xFFF8) == 0x0880 || (op & 0xFFF8) == 0x08C0) && (ext & 31) < 16)
        return -2;
    if (((op & 0xF1F8) == 0x0140 || (op & 0xF1F8) == 0x0180 || (op & 0xF1F8) == 0x01C0) &&
        (c->d[(op >> 9) & 7] & 31) < 16)
        return -2;
    (void)reg;
    return 0;
}

/* Muestreo de interrupciones del 68000 tal como se comporta en MAME (el oráculo).
 *
 * El 68000 decide si atiende una interrupción en un paso fijo de cada instrucción (el
 * "muestreo"). MAME, además, solo procesa una IRQ activada en T cuando la CPU se detiene, y solo
 * se detiene al terminar un acceso al bus o una instrucción en T o después: los muestreos
 * anteriores a ese punto no la ven. Resultado: una IRQ activada en T se atiende al terminar la
 * instrucción j si T <= L_j, donde L_j es el último punto de parada anterior o igual al muestreo
 * de j: el fin del último acceso al bus antes del muestreo o, si no hay ninguno, el inicio de j.
 *
 * Clases (reglas por familia, escritas a partir de la descripción del microcódigo que MAME
 * documenta en m68000-sdf.cpp y verificadas opcode por opcode con
 * tools/sampling_queues.py + tools/verify_sampling.py):
 *   desde el final:  L = fin - l, U = fin - u      (l = -1: sin acceso antes del muestreo)
 *   desde el inicio: L = U = inicio + x            (la duración varía después del último acceso)
 */
typedef struct {
    int8_t l, u;          /* desde el final (l = -1: ninguno) */
    int8_t x;             /* > 0: desde el inicio, L = U = inicio + x */
} AccessClass;

static inline AccessClass access_class(uint32_t op, int duracion)
{
    const AccessClass def = {4, 0, 0}, nin = {-1, 0, 0}, c22 = {2, 2, 0}, c44 = {4, 4, 0}, c60 = {6, 0, 0};
    uint32_t hi = op >> 12, opm = (op >> 6) & 7, mode = (op >> 3) & 7, reg = op & 7;
    switch (hi) {
    case 0x0:
        if ((op & 0xFF00) == 0x0C00 && (op & 0xC0) == 0x80 && mode == 0) return c22;   /* CMPI.L #,Dn */
        if ((op & 0xF100) == 0x0000 && (op & 0xC0) == 0x80 && mode == 0 &&
            (op & 0x0E00) != 0x0800 && (op & 0x0E00) != 0x0C00) return c44;           /* ADDI/SUBI/ANDI/ORI/EORI.L #,Dn */
        if ((op & 0xFFC0) == 0x0800) return mode == 0 ? c22 : def;                     /* BTST #,<ea> */
        if ((op & 0xFF00) == 0x0800 && mode == 0) return (AccessClass){0, 0, 8};       /* BCHG/BCLR/BSET #,Dn */
        if ((op & 0xF1C0) == 0x0100)                                                    /* BTST Dn,<ea> */
            return (mode == 0 || (mode == 7 && reg == 4)) ? c22 : def;
        if ((op & 0xF100) == 0x0100 && mode == 0) return (AccessClass){0, 0, 4};       /* BCHG/BCLR/BSET Dn,Dn */
        return def;
    case 0x1:                                                                           /* MOVE.B */
        return (opm == 0 && mode == 0) ? nin : def;
    case 0x2: case 0x3:                                                                 /* MOVE/MOVEA .L/.W */
        if (opm <= 1 && mode <= 1) return nin;
        return def;
    case 0x4:
        if ((op & 0xFFF0) == 0x4E60) return nin;                                        /* MOVE USP */
        if (op == 0x4E71) return nin;                                                   /* NOP */
        if ((op & 0xF1F8) == 0x41D0) return nin;                                        /* LEA (An),An */
        if ((op & 0xF1F8) == 0x41F0 || (op & 0xF1FF) == 0x41FB) return c60;            /* LEA d8(An,Xi)/d8(PC,Xi) */
        if ((op & 0xFFF8) == 0x4840 || (op & 0xFFB8) == 0x4880) return nin;            /* SWAP, EXT */
        if ((op & 0xFFF8) == 0x4AC0) return nin;                                        /* TAS Dn */
        if ((op & 0xFFF8) == 0x4800 || (op & 0xFFF8) == 0x40C0) return c22;            /* NBCD Dn, MOVE SR,Dn */
        if (((op & 0xFF00) == 0x4000 || (op & 0xFF00) == 0x4200 || (op & 0xFF00) == 0x4400 ||
             (op & 0xFF00) == 0x4600) && mode == 0) {                                   /* NEGX/CLR/NEG/NOT Dn */
            uint32_t tam = (op >> 6) & 3;
            if (tam == 2) return c22;
            if (tam <= 1) return nin;
        }
        if ((op & 0xFF00) == 0x4A00) {                                                  /* TST */
            uint32_t tam = (op >> 6) & 3;
            if (tam == 3) return def;
            if (mode == 0) return nin;
        }
        return def;
    case 0x5:
        if ((op & 0xC0) == 0xC0) {                                                      /* Scc / DBcc */
            if ((op & 0x38) == 0x08) return def;                                        /* DBcc: siempre (4,0) */
            if (mode == 0) return duracion >= 6 ? c22 : nin;                            /* Scc Dn */
            return def;
        }
        {
            uint32_t tam = (op >> 6) & 3;
            if (mode == 0) return tam == 2 ? c44 : nin;                                 /* ADDQ/SUBQ Dn */
            if (mode == 1) return c44;                                                  /* ADDQ/SUBQ An */
        }
        return def;
    case 0x6:                                                                           /* Bcc/BRA/BSR */
        if ((op & 0xFF) != 0 && (op & 0x0F00) != 0x0100 && duracion == 8) return nin;  /* Bcc.b no tomado */
        return def;
    case 0x7:
        return nin;                                                                     /* MOVEQ */
    case 0x8: case 0x9: case 0xB: case 0xC: case 0xD:
        if (opm <= 2) {                                                                 /* <ea>,Dn */
            if (opm == 2) {
                if (mode <= 1 || (mode == 7 && reg == 4)) return hi == 0xB ? c22 : c44;
                return c22;
            }
            if (opm == 0) return mode == 0 ? nin : def;
            return mode <= 1 ? nin : def;
        }
        if (opm == 3 || opm == 7) {
            if (hi == 0x8 || hi == 0xC) {                                               /* DIVU/DIVS, MULU/MULS */
                int x = mode == 0 ? 4 : (mode == 2 || mode == 3 || (mode == 7 && reg == 4)) ? 8 :
                        mode == 4 ? 10 : (mode == 5 || (mode == 7 && (reg == 0 || reg == 2))) ? 12 :
                        (mode == 6 || (mode == 7 && reg == 3)) ? 14 : 16;
                if (hi == 0xC) return (AccessClass){0, 0, (int8_t)x};
                /* DIV muestrea antes del cálculo: con Dn, al empezar (MAME, cuadro 8032 de la
                   grabación: DIVU D0,D1 de 128 cycles no atiende una IRQ activada 18 cycles después
                   de empezar). Con operando en memoria se supone el fin de la lectura del operando
                   (sin verificar: no aparece en la grabación). */
                if (mode == 0) return nin;
                return (AccessClass){0, 0, (int8_t)(x - 4)};
            }
            if (hi == 0xB) return c22;                                                  /* CMPA */
            if (opm == 3) return c44;                                                   /* ADDA/SUBA.W */
            if (mode <= 1 || (mode == 7 && reg == 4)) return c44;                       /* ADDA/SUBA.L reg/imm */
            return c22;                                                                 /* ADDA/SUBA.L memoria */
        }
        if (mode <= 1) {                                                                /* formas de registro */
            if (hi == 0xB) {
                if (mode == 1) return def;                                              /* CMPM */
                return opm == 6 ? c44 : nin;                                            /* EOR Dn,Dn */
            }
            if (hi == 0xC) {
                if (opm == 4 && mode == 0) return c22;                                  /* ABCD */
                if (opm == 5 || opm == 6) return c22;                                   /* EXG */
                return def;
            }
            if (hi == 0x8) return (opm == 4 && mode == 0) ? c22 : def;                  /* SBCD */
            if (mode == 0) return opm == 6 ? c44 : nin;                                 /* ADDX/SUBX Dn */
            return def;
        }
        return def;
    case 0xE:
        if ((op & 0xC0) != 0xC0) return (AccessClass){0, 0, 4};                         /* desplazamientos Dn */
        return def;
    default:
        return def;
    }
}

/* L de la última instrucción de c (ver arriba). Una IRQ activada en T se atiende al terminar esa
 * instrucción si T <= L. */
static inline int64_t cycles_access_l(const Cpu68k *c)
{
    int64_t fin = c->cycles;
    AccessClass k = access_class(c->op, (int)(fin - c->start_cycle));
    if (k.x > 0) return c->start_cycle + k.x;
    if (k.l >= 0) return fin - k.l;
    return c->start_cycle;
}

/* Ciclos exactos de DIVU.W / DIVS.W sin el cálculo de la dirección efectiva, según el
 * algoritmo de temporización de la división del 68000 descrito por Jorge Cwik ("68000 division
 * timing", documentación pública del microcódigo). El divisor no es cero. */
static inline int cycles_divu_exact(uint32_t dividendo, uint16_t divisor)
{
    if ((dividendo >> 16) >= divisor) return 10;            /* desbordamiento */
    int m = 38;
    uint32_t alto = (uint32_t)divisor << 16;
    for (int i = 0; i < 15; i++) {
        uint32_t previo = dividendo;
        dividendo <<= 1;
        if ((int32_t)previo < 0) {
            dividendo -= alto;
        } else {
            m += 2;
            if (dividendo >= alto) { dividendo -= alto; m--; }
        }
    }
    return m * 2;
}

static inline int cycles_divs_exact(int32_t dividendo, int16_t divisor)
{
    int m = 6;
    if (dividendo < 0) m++;
    uint32_t abs_dvd = dividendo < 0 ? (uint32_t)0 - (uint32_t)dividendo : (uint32_t)dividendo;
    uint32_t abs_dvs = divisor < 0 ? (uint32_t)(-(int32_t)divisor) : (uint32_t)divisor;
    if ((abs_dvd >> 16) >= abs_dvs) return (m + 2) * 2;     /* desbordamiento absoluto */
    uint32_t cociente = abs_dvd / abs_dvs;
    m += 55;
    if (divisor >= 0) {
        if (dividendo >= 0) m--;
        else m++;
    }
    for (int i = 0; i < 15; i++) {
        if ((int16_t)cociente >= 0) m++;
        cociente <<= 1;
    }
    return m * 2;
}

/* Diferencia contra la tabla de Musashi (DIVU = 140, DIVS = 158 más la dirección efectiva). */
static inline int cycles_adjust_divu(uint32_t dividendo, uint32_t divisor)
{
    return cycles_divu_exact(dividendo, (uint16_t)divisor) - 140;
}

static inline int cycles_adjust_divs(uint32_t dividendo, uint32_t divisor)
{
    return cycles_divs_exact((int32_t)dividendo, (int16_t)divisor) - 158;
}

/* Palabra fuente de DIVU/DIVS (sin efectos: todos los modos salvo (An)+ y -(An)).
 * Devuelve 0 en *ok si el mode no se evalúa aquí (el ajuste de la división se omite). */
/* Dirección de los modos indexados d8(base,Xi) a partir de su palabra de extensión. */
static inline uint32_t cycles_indexed(const Cpu68k *c, uint32_t base, uint32_t ext)
{
    uint32_t xi = (ext >> 12) & 7;
    uint32_t v = (ext & 0x8000) ? c->a[xi] : c->d[xi];
    if (!(ext & 0x0800)) v = (uint32_t)(int16_t)v;
    return base + (uint32_t)(int8_t)ext + v;
}

static inline uint32_t cycles_source_w(const Cpu68k *c, uint32_t op, uint32_t ext_dir, int *ok)
{
    uint32_t mode = (op >> 3) & 7, reg = op & 7;
    *ok = 1;
    switch (mode) {
    case 0: return c->d[reg] & 0xFFFF;
    case 2: return bus_r16(c->a[reg]);
    case 5: return bus_r16(c->a[reg] + (uint32_t)(int16_t)bus_r16(ext_dir));
    case 6: return bus_r16(cycles_indexed(c, c->a[reg], bus_r16(ext_dir)));
    case 7:
        if (reg == 0) return bus_r16((uint32_t)(int16_t)bus_r16(ext_dir));
        if (reg == 1) return bus_r16(bus_r32(ext_dir));
        if (reg == 2) return bus_r16(ext_dir + (uint32_t)(int16_t)bus_r16(ext_dir));
        if (reg == 3) return bus_r16(cycles_indexed(c, ext_dir, bus_r16(ext_dir)));
        if (reg == 4) return bus_r16(ext_dir);
        break;
    default: break;
    }
    *ok = 0;
    return 0;
}

/* MULS de Musashi: cuenta transiciones de la palabra extendida en signo hasta que no quedan bits
 * (pierde la última transición 1 -> 0); el lado Musashi se corrige con la diferencia. */
static inline int cycles_muls_musashi(uint32_t s)
{
    int n = 0;
    uint32_t f = 0;
    for (uint32_t y = (uint32_t)(int32_t)(int16_t)(s & 0xFFFF); y; y >>= 1)
        if ((y & 1) != f) { n += 2; f = 1 - f; }
    return n;
}

/* Ajuste total de la instrucción en c->pc, con el estado previo (lado Musashi). */
static inline int cycles_adjust_previous(const Cpu68k *c)
{
    uint32_t pc = c->pc & 0xFFFFFF;
    uint32_t op = bus_r16(pc), ext = bus_r16(pc + 2);
    int aj = cycles_adjust(c, op, ext);
    if ((op & 0xF1C0) == 0xC1C0) {                       /* MULS */
        int ok;
        uint32_t s = cycles_source_w(c, op, pc + 2, &ok);
        if (ok) aj += cycles_muls(s) - cycles_muls_musashi(s);
    }
    int divu = (op & 0xF1C0) == 0x80C0, divs = (op & 0xF1C0) == 0x81C0;
    if (divu || divs) {
        int ok;
        uint32_t s = cycles_source_w(c, op, pc + 2, &ok);
        if (ok && s != 0)
            aj += divu ? cycles_adjust_divu(c->d[(op >> 9) & 7], s) : cycles_adjust_divs(c->d[(op >> 9) & 7], s);
    }
    return aj;
}

#endif
