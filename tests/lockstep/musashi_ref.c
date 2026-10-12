/* Referencia del lockstep: Musashi puro, instrucción por instrucción, desde el reset. */
#include "m68k.h"
#include "m68kcpu.h"
#include "ciclos68k.h"

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


static int ajuste_actual(void)
{
    Cpu68k previo = {0};
    for (int i = 0; i < 8; i++) {
        previo.d[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_D0 + i));
        previo.a[i] = m68k_get_reg(NULL, (m68k_register_t)(M68K_REG_A0 + i));
    }
    previo.pc = m68k_get_reg(NULL, M68K_REG_PC);
    return ciclos_ajuste_previo(&previo);
}

/* Ciclos que el código generado suma ANTES de ejecutar la instrucción: base + ajuste. */
int ref_ciclos_base(unsigned pc) { return CYC_INSTRUCTION[m68k_read_memory_16(pc) & 0xFFFF] + ajuste_actual(); }

/* Una instrucción, con el mismo ajuste de ciclos que el código generado (ciclos68k.h). */
int ref_ejecutar_una(void)
{
    int ajuste = ajuste_actual();
    return m68k_execute(1) + ajuste;
}
int ref_detenido(void) { return CPU_STOPPED != 0; }
void ref_despertar(void) { CPU_STOPPED = 0; }
unsigned ref_reg(int r) { return m68k_get_reg(NULL, (m68k_register_t)r); }
