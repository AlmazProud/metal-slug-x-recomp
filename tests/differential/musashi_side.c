/* Musashi side of the differential harness: isolated in its own file because m68kcpu.h defines
 * many internal names. */
#include "m68k.h"
#include "m68kcpu.h"
#include "test_memory.h"
#include "state.h"
#include "cycles68k.h"

unsigned int m68k_read_memory_8(unsigned int a)  { return mem_read8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return (unsigned)mem_read8(a) << 8 | mem_read8(a + 1); }
unsigned int m68k_read_memory_32(unsigned int a) { return m68k_read_memory_16(a) << 16 | m68k_read_memory_16(a + 2); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return m68k_read_memory_16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return m68k_read_memory_32(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v)  { mem_write8(a, (uint8_t)v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { mem_write8(a, (uint8_t)(v >> 8)); mem_write8(a + 1, (uint8_t)v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { m68k_write_memory_16(a, v >> 16); m68k_write_memory_16(a + 2, v & 0xFFFF); }

void ms_init(void)
{
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    m68k_execute(1);            /* consumes the reset cycles (executes no instructions) */
}

/* Executes one instruction from `e` and leaves the result in `e`. Returns the cycles. */
int ms_execute(State *e)
{
    CPU_STOPPED = 0;
    m68k_set_reg(M68K_REG_SR, e->sr);
    m68k_set_reg(M68K_REG_USP, e->usp);
    m68k_set_reg(M68K_REG_ISP, e->ssp);
    for (int i = 0; i < 8; i++) {
        m68k_set_reg(M68K_REG_D0 + i, e->d[i]);
        if (i < 7) m68k_set_reg(M68K_REG_A0 + i, e->a[i]);
    }
    m68k_set_reg(M68K_REG_PC, e->pc);
    /* Same cycle adjustment as the generated code (src/cpu/cycles68k.h). */
    Cpu68k prev = {0};
    for (int i = 0; i < 8; i++) { prev.d[i] = e->d[i]; prev.a[i] = e->a[i]; }
    prev.a[7] = (e->sr & 0x2000) ? e->ssp : e->usp;
    prev.pc = e->pc;
    int adjustment = cycles_adjust_previous(&prev);
    int cycles = m68k_execute(1) + adjustment;
    for (int i = 0; i < 8; i++) {
        e->d[i] = m68k_get_reg(NULL, M68K_REG_D0 + i);
        e->a[i] = m68k_get_reg(NULL, M68K_REG_A0 + i);
    }
    e->sr = m68k_get_reg(NULL, M68K_REG_SR);
    e->usp = m68k_get_reg(NULL, M68K_REG_USP);
    e->ssp = m68k_get_reg(NULL, M68K_REG_ISP);
    e->pc = m68k_get_reg(NULL, M68K_REG_PC);
    return cycles;
}
