#include "dispatcher.h"

#include <stddef.h>

int64_t rc_cycle_limit = INT64_MAX;
volatile int rc_exit = 0;
volatile int rc_irq_level = 0;
int rc_interpreter_only = 0;
int cpu_turbo = 0;
uint64_t rc_recompiled_steps = 0, rc_interpreted_steps = 0;
void (*rc_trace)(const Cpu68k *c) = NULL;

/* Direct per-region tables: P1 (1 MB) and BIOS (128 KB), one entry per even address. */
static rc_fn table_p1[0x100000 / 2];
static rc_fn table_bios[0x20000 / 2];

void rc_register(uint32_t addr, rc_fn f)
{
    addr &= 0xFFFFFF;
    if (addr < 0x100000) table_p1[addr >> 1] = f;
    else if (addr >= 0xC00000 && addr < 0xC20000) table_bios[(addr - 0xC00000) >> 1] = f;
}

rc_fn rc_lookup(uint32_t pc)
{
    pc &= 0xFFFFFF;
    if (pc & 1) return NULL;
    if (pc < 0x100000) return table_p1[pc >> 1];
    if (pc >= 0xC00000 && pc < 0xC20000) return table_bios[(pc - 0xC00000) >> 1];
    return NULL;
}

void rc_step(Cpu68k *c)
{
    if (c->cycles >= rc_cycle_limit || rc_irq_level > c->imask) {
        rc_event(c);
        if (rc_exit) return;
    }
    if (c->stopped) {                 /* STOP: the host decides when to wake up */
        c->cycles = rc_cycle_limit;
        return;
    }
    c->pc &= 0xFFFFFF;
    if (rc_trace) rc_trace(c);
    /* The generated code exits as soon as c->cycles reaches the limit: if the host left a limit
     * that has already expired, an interpreted instruction guarantees progress. */
    rc_fn f = (c->cycles < rc_cycle_limit && !rc_interpreter_only) ? rc_lookup(c->pc) : NULL;
    if (f) { rc_recompiled_steps++; f(c); }
    else   { rc_interpreted_steps++; rc_interpreter_step(c); }
}

void rc_run(Cpu68k *c)
{
    while (!rc_exit) rc_step(c);
}

int rc_call(Cpu68k *c, uint32_t target, uint32_t ret_addr)
{
    const uint32_t ret_sp = c->a[7] + 4;     /* SP expected after the target's RTS */
    c->pc = target & 0xFFFFFF;
    for (;;) {
        if (c->pc == ret_addr && c->a[7] == ret_sp) return 1;
        /* The stack rose above this frame (the target discarded its return address) or an exit
         * was requested: this native level no longer matches any 68000 call. */
        if (rc_exit || c->a[7] > ret_sp) return 0;
        rc_step(c);
    }
}
