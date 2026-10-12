/* Adjustments to the Musashi cycle table so it matches the real 68000 (MAME, the oracle).
 *
 * The base table is still Musashi's (dumped at build time); these functions return the
 * difference to add. Each rule was verified by measuring in MAME (tools/mame/cycles.lua and
 * tools/audit_cycles.py, docs/PHASE4_VERIFICATION.md). They are used by the generated code and
 * the fallback interpreter, with the 68000 state from BEFORE the instruction, so both paths
 * keep producing the same cycles.
 */
#ifndef CYCLES68K_H
#define CYCLES68K_H

#include <stdint.h>
#include <stdlib.h>

#include "cpu68k.h"

/* Cycle cost of an instruction, with the optional Phase 5 "turbo": with cpu_turbo = k each
 * instruction costs 2^k times less, like a faster 68000 driving the same hardware (removes the
 * original slowdown). With 0 (the default) nothing changes. */
#ifndef CIC
extern int cpu_turbo;
#define CIC(n) ((n) >> cpu_turbo)
#endif

/* Per-opcode adjustment (plus its first extension word) that does not depend on the division. */
static inline int cycles_adjust(const Cpu68k *c, uint32_t op, uint32_t ext)
{
    uint32_t mode = (op >> 3) & 7, reg = op & 7;
    /* ADDA.L / SUBA.L Dn,An and An,An: 8, not 6 (MAME: adda.l D0,A2 = 8). */
    if (((op & 0xF1C0) == 0xD1C0 || (op & 0xF1C0) == 0x91C0) && mode <= 1) return 2;
    /* ADD/SUB/AND/OR.L <ea>,Dn with a Dn or An source: 8, not 6 (68000 manual, note "**";
       MAME: add.l D4,D7 = 8). */
    if (((op & 0xF1C0) == 0xD080 || (op & 0xF1C0) == 0x9080 || (op & 0xF1C0) == 0xC080 ||
         (op & 0xF1C0) == 0x8080) && mode <= 1) return 2;
    /* ADDA.W / SUBA.W #imm,An: 12, not 14 (MAME: adda.w #$28,A0 and suba.w #$28,A0 = 12). */
    if (((op & 0xF1FF) == 0xD0FC || (op & 0xF1FF) == 0x90FC)) return -2;
    /* ANDI.L #imm,Dn: 16, not 14 (MAME: andi.l #$ffff,D1 = 16). */
    if ((op & 0xFFF8) == 0x0280) return 2;
    /* ADDQ.W #n,An: 8, not 4 (MAME: addq.w #1,A0 = 8; SUBQ.W was already 8). */
    if ((op & 0xF1F8) == 0x5048) return 4;
    /* BCHG/BCLR/BSET on Dn with bit number < 16: 2 cycles less than the table maximum
       (MAME: bset #7,D0 = 10; bclr #2,D1 = 12; bset D0,D7 = 6). BTST is unchanged. */
    if (((op & 0xFFF8) == 0x0840 || (op & 0xFFF8) == 0x0880 || (op & 0xFFF8) == 0x08C0) && (ext & 31) < 16)
        return -2;
    if (((op & 0xF1F8) == 0x0140 || (op & 0xF1F8) == 0x0180 || (op & 0xF1F8) == 0x01C0) &&
        (c->d[(op >> 9) & 7] & 31) < 16)
        return -2;
    (void)reg;
    return 0;
}

/* 68000 interrupt sampling as it behaves in MAME (the oracle).
 *
 * The 68000 decides whether to take an interrupt at a fixed step of each instruction (the
 * "sampling"). MAME, in addition, only processes an IRQ raised at T when the CPU stops, and it
 * only stops at the end of a bus access or of an instruction at T or later: samplings before
 * that point do not see it. Result: an IRQ raised at T is taken at the end of instruction j if
 * T <= L_j, where L_j is the last stop point before or at the sampling of j: the end of the
 * last bus access before the sampling or, if there is none, the start of j.
 *
 * Classes (per-family rules, written from the microcode description MAME documents in
 * m68000-sdf.cpp and verified opcode by opcode with
 * tools/sampling_queues.py + tools/verify_sampling.py):
 *   from the end:   L = end - l, U = end - u        (l = -1: no access before the sampling)
 *   from the start: L = U = start + x               (the duration varies after the last access)
 */
typedef struct {
    int8_t l, u;          /* from the end (l = -1: none) */
    int8_t x;             /* > 0: from the start, L = U = start + x */
} AccessClass;

static inline AccessClass access_class(uint32_t op, int duration)
{
    const AccessClass def = {4, 0, 0}, none = {-1, 0, 0}, c22 = {2, 2, 0}, c44 = {4, 4, 0}, c60 = {6, 0, 0};
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
        return (opm == 0 && mode == 0) ? none : def;
    case 0x2: case 0x3:                                                                 /* MOVE/MOVEA .L/.W */
        if (opm <= 1 && mode <= 1) return none;
        return def;
    case 0x4:
        if ((op & 0xFFF0) == 0x4E60) return none;                                       /* MOVE USP */
        if (op == 0x4E71) return none;                                                  /* NOP */
        if ((op & 0xF1F8) == 0x41D0) return none;                                       /* LEA (An),An */
        if ((op & 0xF1F8) == 0x41F0 || (op & 0xF1FF) == 0x41FB) return c60;            /* LEA d8(An,Xi)/d8(PC,Xi) */
        if ((op & 0xFFF8) == 0x4840 || (op & 0xFFB8) == 0x4880) return none;           /* SWAP, EXT */
        if ((op & 0xFFF8) == 0x4AC0) return none;                                       /* TAS Dn */
        if ((op & 0xFFF8) == 0x4800 || (op & 0xFFF8) == 0x40C0) return c22;            /* NBCD Dn, MOVE SR,Dn */
        if (((op & 0xFF00) == 0x4000 || (op & 0xFF00) == 0x4200 || (op & 0xFF00) == 0x4400 ||
             (op & 0xFF00) == 0x4600) && mode == 0) {                                   /* NEGX/CLR/NEG/NOT Dn */
            uint32_t size = (op >> 6) & 3;
            if (size == 2) return c22;
            if (size <= 1) return none;
        }
        if ((op & 0xFF00) == 0x4A00) {                                                  /* TST */
            uint32_t size = (op >> 6) & 3;
            if (size == 3) return def;
            if (mode == 0) return none;
        }
        return def;
    case 0x5:
        if ((op & 0xC0) == 0xC0) {                                                      /* Scc / DBcc */
            if ((op & 0x38) == 0x08) return def;                                        /* DBcc: always (4,0) */
            if (mode == 0) return duration >= 6 ? c22 : none;                           /* Scc Dn */
            return def;
        }
        {
            uint32_t size = (op >> 6) & 3;
            if (mode == 0) return size == 2 ? c44 : none;                               /* ADDQ/SUBQ Dn */
            if (mode == 1) return c44;                                                  /* ADDQ/SUBQ An */
        }
        return def;
    case 0x6:                                                                           /* Bcc/BRA/BSR */
        if ((op & 0xFF) != 0 && (op & 0x0F00) != 0x0100 && duration == 8) return none; /* Bcc.b not taken */
        return def;
    case 0x7:
        return none;                                                                    /* MOVEQ */
    case 0x8: case 0x9: case 0xB: case 0xC: case 0xD:
        if (opm <= 2) {                                                                 /* <ea>,Dn */
            if (opm == 2) {
                if (mode <= 1 || (mode == 7 && reg == 4)) return hi == 0xB ? c22 : c44;
                return c22;
            }
            if (opm == 0) return mode == 0 ? none : def;
            return mode <= 1 ? none : def;
        }
        if (opm == 3 || opm == 7) {
            if (hi == 0x8 || hi == 0xC) {                                               /* DIVU/DIVS, MULU/MULS */
                int x = mode == 0 ? 4 : (mode == 2 || mode == 3 || (mode == 7 && reg == 4)) ? 8 :
                        mode == 4 ? 10 : (mode == 5 || (mode == 7 && (reg == 0 || reg == 2))) ? 12 :
                        (mode == 6 || (mode == 7 && reg == 3)) ? 14 : 16;
                if (hi == 0xC) return (AccessClass){0, 0, (int8_t)x};
                /* DIV samples before the computation: with Dn, at the start (MAME, frame 8032 of
                   the recording: a 128-cycle DIVU D0,D1 does not take an IRQ raised 18 cycles after
                   it starts). With a memory operand the end of the operand read is assumed
                   (unverified: it does not appear in the recording). */
                if (mode == 0) return none;
                return (AccessClass){0, 0, (int8_t)(x - 4)};
            }
            if (hi == 0xB) return c22;                                                  /* CMPA */
            if (opm == 3) return c44;                                                   /* ADDA/SUBA.W */
            if (mode <= 1 || (mode == 7 && reg == 4)) return c44;                       /* ADDA/SUBA.L reg/imm */
            return c22;                                                                 /* ADDA/SUBA.L memory */
        }
        if (mode <= 1) {                                                                /* register forms */
            if (hi == 0xB) {
                if (mode == 1) return def;                                              /* CMPM */
                return opm == 6 ? c44 : none;                                           /* EOR Dn,Dn */
            }
            if (hi == 0xC) {
                if (opm == 4 && mode == 0) return c22;                                  /* ABCD */
                if (opm == 5 || opm == 6) return c22;                                   /* EXG */
                return def;
            }
            if (hi == 0x8) return (opm == 4 && mode == 0) ? c22 : def;                  /* SBCD */
            if (mode == 0) return opm == 6 ? c44 : none;                                /* ADDX/SUBX Dn */
            return def;
        }
        return def;
    case 0xE:
        if ((op & 0xC0) != 0xC0) return (AccessClass){0, 0, 4};                         /* Dn shifts */
        return def;
    default:
        return def;
    }
}

/* L of the last instruction of c (see above). An IRQ raised at T is taken at the end of that
 * instruction if T <= L. */
static inline int64_t cycles_access_l(const Cpu68k *c)
{
    int64_t end = c->cycles;
    AccessClass k = access_class(c->op, (int)(end - c->start_cycle));
    if (k.x > 0) return c->start_cycle + k.x;
    if (k.l >= 0) return end - k.l;
    return c->start_cycle;
}

/* Exact DIVU.W / DIVS.W cycles excluding the effective address computation, following the
 * 68000 division timing algorithm described by Jorge Cwik ("68000 division timing", public
 * microcode documentation). The divisor is non-zero. */
static inline int cycles_divu_exact(uint32_t dividend, uint16_t divisor)
{
    if ((dividend >> 16) >= divisor) return 10;             /* overflow */
    int m = 38;
    uint32_t high = (uint32_t)divisor << 16;
    for (int i = 0; i < 15; i++) {
        uint32_t prev = dividend;
        dividend <<= 1;
        if ((int32_t)prev < 0) {
            dividend -= high;
        } else {
            m += 2;
            if (dividend >= high) { dividend -= high; m--; }
        }
    }
    return m * 2;
}

static inline int cycles_divs_exact(int32_t dividend, int16_t divisor)
{
    int m = 6;
    if (dividend < 0) m++;
    uint32_t abs_dvd = dividend < 0 ? (uint32_t)0 - (uint32_t)dividend : (uint32_t)dividend;
    uint32_t abs_dvs = divisor < 0 ? (uint32_t)(-(int32_t)divisor) : (uint32_t)divisor;
    if ((abs_dvd >> 16) >= abs_dvs) return (m + 2) * 2;     /* absolute overflow */
    uint32_t quotient = abs_dvd / abs_dvs;
    m += 55;
    if (divisor >= 0) {
        if (dividend >= 0) m--;
        else m++;
    }
    for (int i = 0; i < 15; i++) {
        if ((int16_t)quotient >= 0) m++;
        quotient <<= 1;
    }
    return m * 2;
}

/* Difference against the Musashi table (DIVU = 140, DIVS = 158 plus the effective address). */
static inline int cycles_adjust_divu(uint32_t dividend, uint32_t divisor)
{
    return cycles_divu_exact(dividend, (uint16_t)divisor) - 140;
}

static inline int cycles_adjust_divs(uint32_t dividend, uint32_t divisor)
{
    return cycles_divs_exact((int32_t)dividend, (int16_t)divisor) - 158;
}

/* Source word of DIVU/DIVS (side-effect free: every mode except (An)+ and -(An)).
 * Sets *ok to 0 if the mode is not evaluated here (the division adjustment is skipped). */
/* Address of the indexed modes d8(base,Xi) from their extension word. */
static inline uint32_t cycles_indexed(const Cpu68k *c, uint32_t base, uint32_t ext)
{
    uint32_t xi = (ext >> 12) & 7;
    uint32_t v = (ext & 0x8000) ? c->a[xi] : c->d[xi];
    if (!(ext & 0x0800)) v = (uint32_t)(int16_t)v;
    return base + (uint32_t)(int8_t)ext + v;
}

static inline uint32_t cycles_source_w(const Cpu68k *c, uint32_t op, uint32_t ext_addr, int *ok)
{
    uint32_t mode = (op >> 3) & 7, reg = op & 7;
    *ok = 1;
    switch (mode) {
    case 0: return c->d[reg] & 0xFFFF;
    case 2: return bus_r16(c->a[reg]);
    case 5: return bus_r16(c->a[reg] + (uint32_t)(int16_t)bus_r16(ext_addr));
    case 6: return bus_r16(cycles_indexed(c, c->a[reg], bus_r16(ext_addr)));
    case 7:
        if (reg == 0) return bus_r16((uint32_t)(int16_t)bus_r16(ext_addr));
        if (reg == 1) return bus_r16(bus_r32(ext_addr));
        if (reg == 2) return bus_r16(ext_addr + (uint32_t)(int16_t)bus_r16(ext_addr));
        if (reg == 3) return bus_r16(cycles_indexed(c, ext_addr, bus_r16(ext_addr)));
        if (reg == 4) return bus_r16(ext_addr);
        break;
    default: break;
    }
    *ok = 0;
    return 0;
}

/* Musashi's MULS: counts transitions of the sign-extended word until no bits are left
 * (it misses the last 1 -> 0 transition); the Musashi side is corrected with the difference. */
static inline int cycles_muls_musashi(uint32_t s)
{
    int n = 0;
    uint32_t f = 0;
    for (uint32_t y = (uint32_t)(int32_t)(int16_t)(s & 0xFFFF); y; y >>= 1)
        if ((y & 1) != f) { n += 2; f = 1 - f; }
    return n;
}

/* Total adjustment of the instruction at c->pc, using the prior state (Musashi side). */
static inline int cycles_adjust_previous(const Cpu68k *c)
{
    uint32_t pc = c->pc & 0xFFFFFF;
    uint32_t op = bus_r16(pc), ext = bus_r16(pc + 2);
    int adj = cycles_adjust(c, op, ext);
    if ((op & 0xF1C0) == 0xC1C0) {                       /* MULS */
        int ok;
        uint32_t s = cycles_source_w(c, op, pc + 2, &ok);
        if (ok) adj += cycles_muls(s) - cycles_muls_musashi(s);
    }
    int divu = (op & 0xF1C0) == 0x80C0, divs = (op & 0xF1C0) == 0x81C0;
    if (divu || divs) {
        int ok;
        uint32_t s = cycles_source_w(c, op, pc + 2, &ok);
        if (ok && s != 0)
            adj += divu ? cycles_adjust_divu(c->d[(op >> 9) & 7], s) : cycles_adjust_divs(c->d[(op >> 9) & 7], s);
    }
    return adj;
}

#endif
