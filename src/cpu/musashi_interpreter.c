/* Fallback interpreter: runs ONE instruction with Musashi, synchronising the state with
 * Cpu68k. Used for code without a recompiled version (RAM, P2 window, code not seen).
 * Musashi's memory functions (m68k_read_memory_*, m68k_write_memory_*) are provided by
 * the host and must use the same bus as the recompiled code. */
#include "cycles68k.h"
#include "dispatcher.h"
#include "m68k.h"
#include "m68kcpu.h"

void rc_interpreter_init(void)
{
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    m68k_execute(1);            /* consumes the reset cycles without running instructions */
}

void rc_interpreter_step(Cpu68k *c)
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

    /* Same convention as the generated code: the instruction's base cycles are added
     * BEFORE executing it, so time-dependent peripherals (line counter) see the same
     * value on both paths. */
    uint32_t prev_pc = c->pc, op = m68k_read_memory_16(c->pc) & 0xFFFF;
    int base = CYC_INSTRUCTION[op];
    int adjust = cycles_adjust_previous(c);         /* with the prior state, like the generator */
    c->op = (uint16_t)op; c->start_cycle = c->cycles;
    c->cycles += CIC(base + adjust);
    c->cycles += CIC(m68k_execute(1)) - CIC(base);

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
    if (CPU_STOPPED) c->stopped = 1;
    (void)prev_pc;
}
