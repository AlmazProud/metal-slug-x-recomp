#include "dispatcher.h"

#include <stddef.h>

int64_t rc_cycle_limit = INT64_MAX;
volatile int rc_exit = 0;
volatile int rc_irq_level = 0;
int rc_interpreter_only = 0;
int cpu_turbo = 0;
uint64_t rc_recompiled_steps = 0, rc_interpreted_steps = 0;
void (*rc_trace)(const Cpu68k *c) = NULL;

/* Tablas directas por región: P1 (1 MB) y BIOS (128 KB), una entrada por dirección par. */
static rc_fn tabla_p1[0x100000 / 2];
static rc_fn tabla_bios[0x20000 / 2];

void rc_register(uint32_t dir, rc_fn f)
{
    dir &= 0xFFFFFF;
    if (dir < 0x100000) tabla_p1[dir >> 1] = f;
    else if (dir >= 0xC00000 && dir < 0xC20000) tabla_bios[(dir - 0xC00000) >> 1] = f;
}

rc_fn rc_lookup(uint32_t pc)
{
    pc &= 0xFFFFFF;
    if (pc & 1) return NULL;
    if (pc < 0x100000) return tabla_p1[pc >> 1];
    if (pc >= 0xC00000 && pc < 0xC20000) return tabla_bios[(pc - 0xC00000) >> 1];
    return NULL;
}

void rc_step(Cpu68k *c)
{
    if (c->cycles >= rc_cycle_limit || rc_irq_level > c->imask) {
        rc_event(c);
        if (rc_exit) return;
    }
    if (c->stopped) {                 /* STOP: el anfitrión decide cuándo despertar */
        c->cycles = rc_cycle_limit;
        return;
    }
    c->pc &= 0xFFFFFF;
    if (rc_trace) rc_trace(c);
    /* El código generado sale en cuanto c->cycles alcanza el límite: si el anfitrión dejó un
     * límite ya vencido, una instrucción interpretada garantiza el avance. */
    rc_fn f = (c->cycles < rc_cycle_limit && !rc_interpreter_only) ? rc_lookup(c->pc) : NULL;
    if (f) { rc_recompiled_steps++; f(c); }
    else   { rc_interpreted_steps++; rc_interpreter_step(c); }
}

void rc_run(Cpu68k *c)
{
    while (!rc_exit) rc_step(c);
}

int rc_call(Cpu68k *c, uint32_t destino, uint32_t retorno)
{
    const uint32_t sp_retorno = c->a[7] + 4;     /* SP esperado tras el RTS del destino */
    c->pc = destino & 0xFFFFFF;
    for (;;) {
        if (c->pc == retorno && c->a[7] == sp_retorno) return 1;
        /* La pila subió por encima de este marco (el destino descartó su retorno) o se pidió
         * salir: este nivel nativo ya no corresponde a ninguna llamada del 68000. */
        if (rc_exit || c->a[7] > sp_retorno) return 0;
        rc_step(c);
    }
}
