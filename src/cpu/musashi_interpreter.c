/* Intérprete de respaldo: ejecuta UNA instrucción con Musashi sincronizando el estado con
 * Cpu68k. Se usa para el código sin versión recompilada (RAM, ventana de P2, código no visto).
 * Las funciones de memoria de Musashi (m68k_read_memory_*, m68k_write_memory_*) las aporta
 * el anfitrión y deben usar el mismo bus que el código recompilado. */
#include "cycles68k.h"
#include "dispatcher.h"
#include "m68k.h"
#include "m68kcpu.h"

void rc_interprete_iniciar(void)
{
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    m68k_execute(1);            /* consume los ciclos del reset sin ejecutar instrucciones */
}

void rc_interprete_paso(Cpu68k *c)
{
    CPU_STOPPED = 0;
    m68k_set_reg(M68K_REG_SR, cpu_get_sr(c));
    m68k_set_reg(M68K_REG_USP, c->s ? c->usp : c->a[7]);
    m68k_set_reg(M68K_REG_ISP, c->s ? c->a[7] : c->ssp);
    for (int i = 0; i < 8; i++) {
        m68k_set_reg(M68K_REG_D0 + i, c->d[i]);
        if (i < 7) m68k_set_reg(M68K_REG_A0 + i, c->a[i]);
    }
    m68k_set_reg(M68K_REG_PC, c->pc);

    /* Misma convención que el código generado: los ciclos base de la instrucción se suman
     * ANTES de ejecutarla, así los periféricos que dependen del tiempo (contador de línea)
     * ven el mismo valor en ambos caminos. */
    uint32_t pc_previo = c->pc, op = m68k_read_memory_16(c->pc) & 0xFFFF;
    int base = CYC_INSTRUCTION[op];
    int ajuste = ciclos_ajuste_previo(c);         /* con el estado previo, como el generador */
    c->op = (uint16_t)op; c->ini = c->ciclos;
    c->ciclos += CIC(base + ajuste);
    c->ciclos += CIC(m68k_execute(1)) - CIC(base);

    for (int i = 0; i < 8; i++) {
        c->d[i] = m68k_get_reg(NULL, M68K_REG_D0 + i);
        c->a[i] = m68k_get_reg(NULL, M68K_REG_A0 + i);
    }
    uint32_t sr = m68k_get_reg(NULL, M68K_REG_SR);
    c->s = (sr >> 13) & 1;
    c->t = (sr >> 15) & 1;
    c->imask = (sr >> 8) & 7;
    cpu_set_ccr(c, sr);
    c->usp = m68k_get_reg(NULL, M68K_REG_USP);
    c->ssp = m68k_get_reg(NULL, M68K_REG_ISP);
    c->pc = m68k_get_reg(NULL, M68K_REG_PC) & 0xFFFFFF;
    if (CPU_STOPPED) c->detenido = 1;
    (void)pc_previo;
}
