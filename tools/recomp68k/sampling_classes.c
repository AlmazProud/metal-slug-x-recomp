/* Queries the interrupt sampling rules of src/cpu/cycles68k.h: reads lines
 * "<opcode hex> <duration>" and writes "L U" measured from the start of the instruction ("N" if
 * there is no bus access before sampling). Used by tools/verify_sampling.py. */
#include <stdio.h>

#include "cycles68k.h"

/* cycles68k.h needs the bus; this query does not use it. */
uint32_t bus_r8(uint32_t a) { (void)a; return 0; }
uint32_t bus_r16(uint32_t a) { (void)a; return 0; }
void bus_w8(uint32_t a, uint32_t v) { (void)a; (void)v; }
void bus_w16(uint32_t a, uint32_t v) { (void)a; (void)v; }

int main(void)
{
    unsigned op;
    int dur;
    while (scanf("%x %d", &op, &dur) == 2) {
        AccessClass k = access_class(op, dur);
        if (k.x > 0) printf("%d %d\n", k.x, k.x);
        else if (k.l < 0) printf("N %d\n", dur - k.u);
        else printf("%d %d\n", dur - k.l, dur - k.u);
    }
    return 0;
}
