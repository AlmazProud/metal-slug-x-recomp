#include "despachador.h"

#include <stddef.h>

int64_t rc_limite_ciclos = INT64_MAX;
volatile int rc_salir = 0;
volatile int rc_irq_nivel = 0;
uint64_t rc_pasos_recompilados = 0, rc_pasos_interpretados = 0;
void (*rc_traza)(const Cpu68k *c) = NULL;

/* Tablas directas por región: P1 (1 MB) y BIOS (128 KB), una entrada por dirección par. */
static rc_fn tabla_p1[0x100000 / 2];
static rc_fn tabla_bios[0x20000 / 2];

void rc_registrar(uint32_t dir, rc_fn f)
{
    dir &= 0xFFFFFF;
    if (dir < 0x100000) tabla_p1[dir >> 1] = f;
    else if (dir >= 0xC00000 && dir < 0xC20000) tabla_bios[(dir - 0xC00000) >> 1] = f;
}

rc_fn rc_buscar(uint32_t pc)
{
    pc &= 0xFFFFFF;
    if (pc & 1) return NULL;
    if (pc < 0x100000) return tabla_p1[pc >> 1];
    if (pc >= 0xC00000 && pc < 0xC20000) return tabla_bios[(pc - 0xC00000) >> 1];
    return NULL;
}

void rc_paso(Cpu68k *c)
{
    if (c->ciclos >= rc_limite_ciclos || rc_irq_nivel > c->imask) {
        rc_evento(c);
        if (rc_salir) return;
    }
    if (c->detenido) {                 /* STOP: el anfitrión decide cuándo despertar */
        c->ciclos = rc_limite_ciclos;
        return;
    }
    c->pc &= 0xFFFFFF;
    if (rc_traza) rc_traza(c);
    /* El código generado sale en cuanto c->ciclos alcanza el límite: si el anfitrión dejó un
     * límite ya vencido, una instrucción interpretada garantiza el avance. */
    rc_fn f = c->ciclos < rc_limite_ciclos ? rc_buscar(c->pc) : NULL;
    if (f) { rc_pasos_recompilados++; f(c); }
    else   { rc_pasos_interpretados++; rc_interprete_paso(c); }
}

void rc_ejecutar(Cpu68k *c)
{
    while (!rc_salir) rc_paso(c);
}

int rc_llamar(Cpu68k *c, uint32_t destino, uint32_t retorno)
{
    const uint32_t sp_retorno = c->a[7] + 4;     /* SP esperado tras el RTS del destino */
    c->pc = destino & 0xFFFFFF;
    for (;;) {
        if (c->pc == retorno && c->a[7] == sp_retorno) return 1;
        /* La pila subió por encima de este marco (el destino descartó su retorno) o se pidió
         * salir: este nivel nativo ya no corresponde a ninguna llamada del 68000. */
        if (rc_salir || c->a[7] > sp_retorno) return 0;
        rc_paso(c);
    }
}
