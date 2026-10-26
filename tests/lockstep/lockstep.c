/* Lockstep of the recompiled program against pure Musashi, from reset and with the same machine.
 *
 * 1) Runs the recompiled program (with Musashi as fallback) up to N cycles and records a
 *    checkpoint at every dispatch: (cycles, PC, register hash).
 * 2) Runs pure Musashi from the same reset, instruction by instruction, and at every
 *    checkpoint requires the same PC and the same registers at the same cycle.
 * 3) Compares the final memory (RAM, SRAM, palette, VRAM, the NEO-PVC's RAM).
 *
 * VBlank (IRQ1, autovector) every frame at line 248. The recompiled side delivers it at a
 * dispatch point and records (cycle, PC); Musashi receives it at exactly that same point.
 * Interrupt entry is emulated the same way on both sides (44 cycles, as in Musashi).
 *
 * Usage: lockstep <game_m68k> <bios_m68k> [cycles] [MAME dump]
 * The cartridge (P2 banks, protection or NEO-SMA chip) is the game descriptor's (ng_game): there
 * is one lockstep executable per game. With a dump (tools/mame/dump_state.lua) both sides start
 * from that game state instead of reset.
 */
#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>

#include "cpu68k.h"
#include "dispatcher.h"
#include "m68k.h"
#include "machine.h"

void program_register(void);
void ref_reset(void);
void ref_wake(void);
int ref_base_cycles(unsigned pc);
int ref_execute_one(void);
int ref_stopped(void);
unsigned ref_reg(int r);

typedef struct { int64_t cycles; uint32_t pc, hash; } Point;

/* MAME dump: 19 registers, vectors, P2 window, RAM, SRAM, palette; version 2 adds the last value
 * written to the NEO-SMA bank register (the window's 16 bytes do not identify an SMA bank);
 * version 3 (NEO-PVC) adds the window's P2 offset and the chip's 8 KB of RAM. */
typedef struct {
    uint32_t r[19];             /* D0-D7, A0-A6, SP, USP, SR, PC */
    uint8_t vectors[128], window[16];
    uint8_t ram[0x10000], sram[0x10000], palette[0x2000];
    int64_t bank_value;         /* -1: none written / version 1 */
    uint8_t cart_ram[0x2000];   /* version 3 */
} Dump;
static Dump dump;
static int have_dump;

static uint32_t be32(FILE *f)
{
    uint8_t b[4];
    if (fread(b, 1, 4, f) != 4) { fprintf(stderr, "short dump\n"); exit(2); }
    return (uint32_t)b[0] << 24 | b[1] << 16 | b[2] << 8 | b[3];
}

static void load_dump(const char *path)
{
    FILE *f = fopen(path, "rb");
    uint8_t magic[6];
    if (!f || fread(magic, 1, 6, f) != 6 || memcmp(magic, "PDEST", 5) || magic[5] < '1' || magic[5] > '3') {
        fprintf(stderr, "invalid dump: %s\n", path);
        exit(2);
    }
    for (int i = 0; i < 19; i++) dump.r[i] = be32(f);
    if (fread(dump.vectors, 1, 128, f) != 128 || fread(dump.window, 1, 16, f) != 16 ||
        fread(dump.ram, 1, sizeof dump.ram, f) != sizeof dump.ram || fread(dump.sram, 1, sizeof dump.sram, f) != sizeof dump.sram ||
        fread(dump.palette, 1, sizeof dump.palette, f) != sizeof dump.palette) { fprintf(stderr, "short dump\n"); exit(2); }
    dump.bank_value = -1;
    if (magic[5] == '2') {
        uint32_t v = be32(f);
        if (v != 0xFFFFFFFFu) dump.bank_value = v & 0xFFFF;
    }
    if (magic[5] == '3') {
        dump.bank_value = be32(f);
        if (fread(dump.cart_ram, 1, sizeof dump.cart_ram, f) != sizeof dump.cart_ram) { fprintf(stderr, "short dump\n"); exit(2); }
    }
    fclose(f);
    have_dump = 1;
}
static Point *points;
static size_t n_points, cap_points;

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

static uint32_t hash_mem(const Machine *m)
{
    uint32_t h = 0x811C9DC5u;
    for (size_t i = 0; i < sizeof m->ram; i++) h = (h ^ m->ram[i]) * 0x01000193u;
    for (size_t i = 0; i < sizeof m->sram; i++) h = (h ^ m->sram[i]) * 0x01000193u;
    for (size_t i = 0; i < sizeof m->palette; i++) h = (h ^ m->palette[i]) * 0x01000193u;
    for (size_t i = 0; i < sizeof m->cart_ram; i++) h = (h ^ m->cart_ram[i]) * 0x01000193u;
    for (size_t i = 0; i < 0x10000; i++) h = fnv(h, m->vram[i]);
    return h;
}

#define FRAME (768 * 264)
#define VBLANK_LINE 248
typedef struct { int64_t cycles; uint32_t pc; } Irq;
static Irq *irqs;
static size_t n_irqs, cap_irqs;
static uint8_t *executed;         /* executed instruction addresses (Musashi side) */

/* ---------------------------------------------------------------- recompiled side */
static int64_t limit, next_vblank;
static int irq_pending;

static void schedule(const Cpu68k *c)
{
    /* Besides VBlank and the end, a checkpoint every ~1000 cycles: with the generated code's
     * per-instruction checkpoints, this verifies the state frequently (a prime number so it
     * does not synchronize with the program's loops). */
    int64_t t = c->cycles + 997;
    if (next_vblank < t) t = next_vblank;
    if (limit < t) t = limit;
    rc_cycle_limit = t;
    rc_irq_level = irq_pending;       /* the dispatcher returns to rc_event if the mask drops */
}

void rc_event(Cpu68k *c)
{
    if (c->cycles >= limit) { rc_exit = 1; return; }
    if (c->cycles >= next_vblank) { irq_pending = 1; next_vblank += FRAME; }
    if (c->stopped && c->cycles < next_vblank && !irq_pending) c->cycles = next_vblank;
    if (irq_pending && c->imask < 1) {
        if (n_irqs == cap_irqs) { cap_irqs = cap_irqs ? cap_irqs * 2 : 1024; irqs = realloc(irqs, cap_irqs * sizeof *irqs); }
        irqs[n_irqs++] = (Irq){c->cycles, c->pc};
        uint32_t sr = cpu_get_sr(c);
        if (!c->s) { c->usp = c->a[7]; c->a[7] = c->ssp; c->s = 1; }
        c->t = 0; c->imask = 1;
        c->a[7] -= 4; bus_w32(c->a[7], c->pc);
        c->a[7] -= 2; bus_w16(c->a[7], sr);
        c->pc = bus_r32(25 * 4) & 0xFFFFFF;
        c->cycles += 44;
        c->stopped = 0;
        irq_pending = 0;
    }
    schedule(c);
}

/* Musashi: the same interrupt entry, done by hand through the register API. */
static void irq_in_musashi(void)
{
    uint32_t sr = ref_reg(M68K_REG_SR), pc = ref_reg(M68K_REG_PC);
    m68k_set_reg(M68K_REG_SR, (sr & ~0x8700u) | 0x2000u | (1u << 8));   /* S=1, T=0, I=1 */
    uint32_t sp = ref_reg(M68K_REG_A7) - 4;
    bus_w32(sp, pc);
    sp -= 2;
    bus_w16(sp, sr);
    m68k_set_reg(M68K_REG_A7, sp);
    m68k_set_reg(M68K_REG_PC, bus_r32(25 * 4));
    ref_wake();
}

static void trace(const Cpu68k *c)
{
    if (n_points == cap_points) {
        cap_points = cap_points ? cap_points * 2 : 1 << 16;
        points = realloc(points, cap_points * sizeof *points);
        if (!points) { fprintf(stderr, "out of memory\n"); exit(3); }
    }
    uint32_t usp = c->s ? c->usp : c->a[7], ssp = c->s ? c->a[7] : c->ssp;
    points[n_points++] = (Point){c->cycles, c->pc, hash_regs(c->d, c->a, cpu_get_sr(c), usp, ssp)};
}

int main(int argc, char **argv)
{
    if (argc < 3) { fprintf(stderr, "usage: %s <game_m68k> <bios_m68k> [cycles] [MAME dump]\n", argv[0]); return 2; }
    limit = argc > 3 ? strtoll(argv[3], NULL, 10) : 20000000;
    if (argc > 4) load_dump(argv[4]);
    mach_load_roms(argv[1], argv[2]);
    setvbuf(stdout, NULL, _IONBF, 0);

    /* ---- 1) recompiled program */
    static Machine mb, ma;
    static Cpu68k c;
    mach_init(&mb, &c.cycles);
    mach_use(&mb);
    rc_interpreter_init();
    program_register();
    memset(&c, 0, sizeof c);
    if (have_dump) {
        mach_from_dump(&mb, dump.vectors, dump.window, dump.bank_value, dump.ram, dump.sram, dump.palette, dump.cart_ram);
        for (int i = 0; i < 8; i++) c.d[i] = dump.r[i];
        for (int i = 0; i < 7; i++) c.a[i] = dump.r[8 + i];
        uint32_t sr = dump.r[17];
        c.s = (sr >> 13) & 1; c.t = (sr >> 15) & 1; c.imask = (sr >> 8) & 7;
        cpu_set_ccr(&c, sr);
        c.a[7] = dump.r[15];
        if (c.s) c.usp = dump.r[16]; else c.ssp = 0;
        c.pc = dump.r[18] & 0xFFFFFF;
    } else {
        c.s = 1; c.imask = 7;
        c.a[7] = bus_r32(0); c.pc = bus_r32(4) & 0xFFFFFF;
    }
    next_vblank = (int64_t)VBLANK_LINE * 768;
    schedule(&c);
    rc_trace = trace;
    clock_t t0 = clock();
    rc_run(&c);
    double sec_b = (double)(clock() - t0) / CLOCKS_PER_SEC;
    uint32_t mem_b = hash_mem(&mb);
    printf("Recompiled: %" PRId64 " cycles, %zu dispatches (%" PRIu64 " recompiled, %" PRIu64
           " interpreted), %.2f s, final pc %06X, stopped=%d\n",
           c.cycles, n_points, rc_recompiled_steps, rc_interpreted_steps, sec_b, c.pc, c.stopped);

    /* ---- 2) pure Musashi */
    int64_t total = 0, machine_clock = 0;
    mach_init(&ma, &machine_clock);
    mach_use(&ma);
    ref_reset();
    if (have_dump) {
        mach_from_dump(&ma, dump.vectors, dump.window, dump.bank_value, dump.ram, dump.sram, dump.palette, dump.cart_ram);
        m68k_set_reg(M68K_REG_SR, dump.r[17]);
        for (int i = 0; i < 8; i++) m68k_set_reg(M68K_REG_D0 + i, dump.r[i]);
        for (int i = 0; i < 7; i++) m68k_set_reg(M68K_REG_A0 + i, dump.r[8 + i]);
        if (dump.r[17] & 0x2000) { m68k_set_reg(M68K_REG_ISP, dump.r[15]); m68k_set_reg(M68K_REG_USP, dump.r[16]); }
        else { m68k_set_reg(M68K_REG_USP, dump.r[15]); m68k_set_reg(M68K_REG_ISP, 0); }
        m68k_set_reg(M68K_REG_PC, dump.r[18]);
    }
    size_t k = 0, ki = 0;
    int ok = 1;
    executed = calloc(0x800000, 1);
    uint64_t instructions = 0;
    t0 = clock();
    while (k < n_points) {
        uint32_t pc = ref_reg(M68K_REG_PC) & 0xFFFFFF;
        if (ki < n_irqs && total == irqs[ki].cycles && pc == irqs[ki].pc) {
            irq_in_musashi();
            total += 44;
            ki++;
            continue;
        }
        if (ref_stopped() && ki < n_irqs && total < irqs[ki].cycles) {
            total = irqs[ki].cycles;          /* STOP: time advances up to the interrupt */
            continue;
        }
        if (total == points[k].cycles) {
            uint32_t d[8], a[8];
            for (int i = 0; i < 8; i++) { d[i] = ref_reg(M68K_REG_D0 + i); a[i] = ref_reg(M68K_REG_A0 + i); }
            uint32_t h = hash_regs(d, a, ref_reg(M68K_REG_SR), ref_reg(M68K_REG_USP), ref_reg(M68K_REG_ISP));
            if (pc != points[k].pc || h != points[k].hash) {
                printf("DIVERGENCE at point %zu (cycle %" PRId64 "): musashi pc=%06X recompiled pc=%06X; "
                       "registers %s\n", k, total, pc, points[k].pc, h == points[k].hash ? "equal" : "DIFFERENT");
                if (k > 0) printf("  last correct point: %zu cycle %" PRId64 " pc=%06X\n",
                                  k - 1, points[k - 1].cycles, points[k - 1].pc);
                printf("  musashi: sr=%04X usp=%08X isp=%08X", ref_reg(M68K_REG_SR), ref_reg(M68K_REG_USP),
                       ref_reg(M68K_REG_ISP));
                for (int i = 0; i < 8; i++) printf(" d%d=%08X", i, d[i]);
                for (int i = 0; i < 8; i++) printf(" a%d=%08X", i, a[i]);
                printf("\n");
                ok = 0;
                break;
            }
            k++;
            continue;
        }
        if (total > points[k].cycles) {
            printf("CYCLE DIVERGENCE: Musashi went past cycle %" PRId64 " without reaching point %zu "
                   "(expected pc %06X; musashi at %06X, cycle %" PRId64 ")\n",
                   points[k].cycles, k, points[k].pc, pc, total);
            ok = 0;
            break;
        }
        if (ref_stopped()) { printf("Musashi stopped (STOP) before point %zu\n", k); ok = 0; break; }
        machine_clock = total + ref_base_cycles(pc);
        executed[pc >> 1] = 1;
        total += ref_execute_one();
        instructions++;
    }
    size_t distinct = 0, in_p1 = 0, in_bios = 0;
    for (uint32_t i = 0; i < 0x800000; i++)
        if (executed[i]) {
            distinct++;
            if (i < 0x80000) in_p1++;
            else if (i >= 0x600000 && i < 0x610000) in_bios++;
        }
    printf("VBlank interrupts delivered: %zu of %zu; addresses executed: %zu (P1 %zu, BIOS %zu)\n",
           ki, n_irqs, distinct, in_p1, in_bios);
    double sec_a = (double)(clock() - t0) / CLOCKS_PER_SEC;
    printf("Musashi: %" PRIu64 " instructions, %.2f s; points verified: %zu of %zu\n",
           instructions, sec_a, k, n_points);

    /* The last section (from the last point up to the limit) is covered by comparing memory
     * after Musashi reaches the same final cycle. */
    if (ok) {
        while (total < c.cycles) {
            uint32_t pc = ref_reg(M68K_REG_PC) & 0xFFFFFF;
            if (ki < n_irqs && total == irqs[ki].cycles && pc == irqs[ki].pc) {
                irq_in_musashi(); total += 44; ki++; continue;
            }
            if (ref_stopped()) { total = c.cycles; break; }
            machine_clock = total + ref_base_cycles(pc);
            total += ref_execute_one();
        }
        uint32_t mem_a = hash_mem(&ma);
        if (total != c.cycles) { printf("Final cycle differs: musashi %" PRId64 " recompiled %" PRId64 "\n", total, c.cycles); ok = 0; }
        if (mem_a != mem_b) { printf("Final memory DIFFERS (ram/sram/palette/vram/cartridge RAM)\n"); ok = 0; }
        else printf("Final memory identical (RAM, SRAM, palette, VRAM, cartridge RAM)\n");
    }
    printf("%s\n", ok ? "LOCKSTEP OK" : "LOCKSTEP FAIL");
    return ok ? 0 : 1;
}
