/* Referencia del lockstep: Musashi puro, instrucción por instrucción, desde el reset. */
#include "m68k.h"
#include "m68kcpu.h"

void ref_reset(void)
{
    m68k_init();
    m68k_set_cpu_type(M68K_CPU_TYPE_68000);
    m68k_pulse_reset();
    m68k_execute(1);
    /* El reset del 68000 no define D0-D7/A0-A6/USP; Musashi conserva lo anterior. Ambos lados
     * de la prueba parten de cero. */
    for (int i = 0; i < 8; i++) {
        m68k_set_reg(M68K_REG_D0 + i, 0);
        if (i < 7) m68k_set_reg(M68K_REG_A0 + i, 0);
    }
    m68k_set_reg(M68K_REG_USP, 0);
}

int ref_ciclos_base(unsigned pc) { return CYC_INSTRUCTION[m68k_read_memory_16(pc) & 0xFFFF]; }
int ref_ejecutar_una(void) { return m68k_execute(1); }
int ref_detenido(void) { return CPU_STOPPED != 0; }
void ref_despertar(void) { CPU_STOPPED = 0; }
unsigned ref_reg(int r) { return m68k_get_reg(NULL, (m68k_register_t)r); }
