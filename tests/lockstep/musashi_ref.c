/* Lockstep reference: pure Musashi, instruction by instruction, from reset. */
#include "m68k.h"
#include "m68kcpu.h"
#include "cycles68k.h"

void ref_reset(void)
{
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    m68k_execute(1);
    /* The 68000 reset does not define D0-D7/A0-A6/USP or the condition codes; Musashi keeps the
     * previous values (a fresh Musashi reads as Z set). Both sides of the test start from zero. */
    for (int i = 0; i < 8; i++) {
        m68k_set_reg(M68K_REG_D0 + i, 0);
        if (i < 7) m68k_set_reg(M68K_REG_A0 + i, 0);
    }
    m68k_set_reg(M68K_REG_USP, 0);
    m68k_set_reg(M68K_REG_SR, m68k_get_reg(NULL, M68K_REG_SR) & ~0x1Fu);
}


static int current_adjustment(void)
{
    Cpu68k prev = {0};
    for (int i = 0; i < 8; i++) {
        prev.d[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + i));
        prev.a[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + i));
    }
    prev.pc = m68k_get_reg(NULL, M68K_REG_PC);
    return cycles_adjust_previous(&prev);
}

/* Cycles the generated code adds BEFORE executing the instruction: base + adjustment. */
int ref_base_cycles(unsigned pc) { return CYC_INSTRUCTION[m68k_read_memory_16(pc) & 0xFFFF] + current_adjustment(); }

/* One instruction, with the same cycle adjustment as the generated code (cycles68k.h). */
int ref_execute_one(void)
{
    int adjustment = current_adjustment();
    return m68k_execute(1) + adjustment;
}
int ref_stopped(void) { return CPU_STOPPED != 0; }
void ref_wake(void) { CPU_STOPPED = 0; }
unsigned ref_reg(int r) { return m68k_get_reg(NULL, (m68k_register_t)r); }
