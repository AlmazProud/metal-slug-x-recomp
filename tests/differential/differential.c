/* Differential tests of the recompiler: every real instruction of the game is executed from
 * random states in Musashi (reference) and in the generated C, and registers, SR, PC and
 * memory writes are compared.
 *
 * Usage: diferencial <mslugx_game_m68k> <mslugx_bios_m68k> [seeds per instruction]
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "cpu68k.h"
#include "state.h"
#include "test_memory.h"
#include "tests.h"

void ms_init(void);
int ms_execute(State *e);

static uint32_t rng;
static uint32_t rand32(void) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; return rng; }

static void random_state(State *e, uint32_t pc)
{
    for (int i = 0; i < 8; i++) { e->d[i] = rand32(); e->a[i] = rand32(); }
    /* Values common in the game: small, zero and edge values, to exercise the flags. */
    if ((rand32() & 3) == 0) e->d[rand32() & 7] = 0;
    if ((rand32() & 3) == 0) e->d[rand32() & 7] = rand32() & 0xFF;
    if ((rand32() & 3) == 0) e->d[rand32() & 7] = 0x80000000u >> (rand32() & 31);
    e->sr = (rand32() & 0x1F) | (rand32() & 1) << 13 | (rand32() & 7) << 8;
    e->usp = rand32();
    e->ssp = rand32();
    e->a[7] = (e->sr & 0x2000) ? e->ssp : e->usp;
    e->pc = pc;
}

static int run_recompiled(const Test *p, State *e)
{
    Cpu68k c;
    memset(&c, 0, sizeof c);
    memcpy(c.d, e->d, sizeof c.d);
    memcpy(c.a, e->a, sizeof c.a);
    c.s = (e->sr >> 13) & 1;
    c.t = (e->sr >> 15) & 1;
    c.imask = (e->sr >> 8) & 7;
    cpu_set_ccr(&c, e->sr);
    c.usp = e->usp;
    c.ssp = e->ssp;
    c.a[7] = c.s ? e->ssp : e->usp;
    c.pc = e->pc;
    p->fn(&c);
    memcpy(e->d, c.d, sizeof c.d);
    memcpy(e->a, c.a, sizeof c.a);
    e->sr = cpu_get_sr(&c);
    if (c.s) { e->ssp = c.a[7]; e->usp = c.usp; } else { e->usp = c.a[7]; e->ssp = c.ssp; }
    e->pc = c.pc;
    return (int)c.cycles;
}

static int compare(const State *m, const State *r, const Layer *cm, const Layer *cr, char *det, size_t n)
{
    int ok = 1;
    size_t k = 0;
#define DIF(...) do { ok = 0; if (k < n) k += (size_t)snprintf(det + k, n - k, __VA_ARGS__); } while (0)
    for (int i = 0; i < 8; i++) {
        if (m->d[i] != r->d[i]) DIF(" d%d musashi=%08X recomp=%08X;", i, m->d[i], r->d[i]);
        if (m->a[i] != r->a[i]) DIF(" a%d musashi=%08X recomp=%08X;", i, m->a[i], r->a[i]);
    }
    if (m->usp != r->usp) DIF(" usp musashi=%08X recomp=%08X;", m->usp, r->usp);
    if (m->ssp != r->ssp) DIF(" ssp musashi=%08X recomp=%08X;", m->ssp, r->ssp);
    if ((m->sr & 0xA71F) != (r->sr & 0xA71F)) DIF(" sr musashi=%04X recomp=%04X;", m->sr & 0xA71F, r->sr & 0xA71F);
    if ((m->pc & 0xFFFFFF) != (r->pc & 0xFFFFFF)) DIF(" pc musashi=%06X recomp=%06X;", m->pc & 0xFFFFFF, r->pc & 0xFFFFFF);
    const Layer *layers[2] = {cm, cr};
    for (int c = 0; c < 2; c++)
        for (int i = 0; i < layers[c]->n; i++) {
            uint32_t a = layers[c]->addr[i];
            uint8_t vm = mem_read8_in(cm, a), vr = mem_read8_in(cr, a);
            if (vm != vr) { DIF(" mem[%06X] musashi=%02X recomp=%02X;", a, vm, vr); break; }
        }
#undef DIF
    return ok;
}

int main(int argc, char **argv)
{
    if (argc < 3) {
        fprintf(stderr, "usage: %s <mslugx_game_m68k> <mslugx_bios_m68k> [seeds]\n", argv[0]);
        return 2;
    }
    int seeds = argc > 3 ? atoi(argv[3]) : 64;
    int debug = getenv("PD_VERBOSE") != NULL;
    setvbuf(stdout, NULL, _IONBF, 0);
    mem_load_roms(argv[1], argv[2]);
    static Layer layer_m, layer_r;
    mem_use_layer(&layer_m);          /* Musashi's reset reads the vectors */
    ms_init();
    int failed = 0, cases = 0;
    for (int t = 0; t < n_tests; t++) {
        const Test *p = &tests[t];
        int failures = 0;
        if (debug) fprintf(stderr, "%d %06X %s\n", t, p->addr, p->text);
        char first[512] = "";
        for (int s = 0; s < seeds; s++) {
            rng = 0x12345678u ^ (uint32_t)(t * 7919 + s * 104729 + 1);
            State initial, em, er;
            random_state(&initial, p->addr);
            mem_seed(rand32());
            em = initial; er = initial;
            layer_m.n = 0; mem_use_layer(&layer_m); int cyc_m = ms_execute(&em);
            layer_r.n = 0; mem_use_layer(&layer_r); int cyc_r = run_recompiled(p, &er);
            cases++;
            char det[400] = "";
            int ok = compare(&em, &er, &layer_m, &layer_r, det, sizeof det);
            if (cyc_m != cyc_r) {
                size_t k = strlen(det);
                snprintf(det + k, sizeof det - k, " cycles musashi=%d recomp=%d;", cyc_m, cyc_r);
                ok = 0;
            }
            if (!ok) {
                if (!failures)
                    snprintf(first, sizeof first, "seed %d, initial_sr=%04X:%s", s, initial.sr, det);
                failures++;
            }
        }
        if (failures) {
            failed++;
            printf("FAIL %06X  %-34s  %d/%d  %s\n", p->addr, p->text, failures, seeds, first);
        }
    }
    printf("\nInstructions tested: %d  cases: %d  instructions with differences: %d\n",
           n_tests, cases, failed);
    return failed ? 1 : 0;
}
