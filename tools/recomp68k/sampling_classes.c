/* Consulta las reglas de muestreo de interrupciones de src/cpu/cycles68k.h: lee líneas
 * "<opcode hex> <duración>" y escribe "L U" medidos desde el inicio de la instrucción ("N" si no
 * hay acceso al bus antes del muestreo). Lo usa tools/verify_sampling.py. */
#include <stdio.h>

#include "cycles68k.h"

/* cycles68k.h necesita el bus; esta consulta no lo usa. */
uint32_t bus_r8(uint32_t a) { (void)a; return 0; }
uint32_t bus_r16(uint32_t a) { (void)a; return 0; }
void bus_w8(uint32_t a, uint32_t v) { (void)a; (void)v; }
void bus_w16(uint32_t a, uint32_t v) { (void)a; (void)v; }

int main(void)
{
    unsigned op;
    int dur;
    while (scanf("%x %d", &op, &dur) == 2) {
        ClaseAcceso k = clase_acceso(op, dur);
        if (k.x > 0) printf("%d %d\n", k.x, k.x);
        else if (k.l < 0) printf("N %d\n", dur - k.u);
        else printf("%d %d\n", dur - k.l, dur - k.u);
    }
    return 0;
}
