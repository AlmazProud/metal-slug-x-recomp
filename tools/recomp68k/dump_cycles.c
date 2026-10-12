/* Vuelca los ciclos base de Musashi para el 68000 (65536 opcodes, un byte cada uno).
 * El generador del recompilador usa esta tabla como costo base de cada instrucción; los
 * ajustes dinámicos (saltos no tomados, desplazamientos, MOVEM, MUL, excepciones) los emite
 * el propio generador. Uso: volcar_ciclos <salida.bin> */
#include <stdio.h>
#include "m68k.h"

extern unsigned char m68ki_cycles[][0x10000];

/* Musashi exige estas funciones aunque aquí no se ejecute código. */
unsigned int m68k_read_memory_8(unsigned int a)  { (void)a; return 0; }
unsigned int m68k_read_memory_16(unsigned int a) { (void)a; return 0; }
unsigned int m68k_read_memory_32(unsigned int a) { (void)a; return 0; }
unsigned int m68k_read_disassembler_16(unsigned int a) { (void)a; return 0; }
unsigned int m68k_read_disassembler_32(unsigned int a) { (void)a; return 0; }
void m68k_write_memory_8(unsigned int a, unsigned int v)  { (void)a; (void)v; }
void m68k_write_memory_16(unsigned int a, unsigned int v) { (void)a; (void)v; }
void m68k_write_memory_32(unsigned int a, unsigned int v) { (void)a; (void)v; }

int main(int argc, char **argv)
{
    if (argc < 2) { fprintf(stderr, "uso: volcar_ciclos <salida.bin>\n"); return 2; }
    m68k_init();
    FILE *f = fopen(argv[1], "wb");
    if (!f || fwrite(m68ki_cycles[0], 1, 0x10000, f) != 0x10000) { fprintf(stderr, "error escribiendo\n"); return 1; }
    fclose(f);
    return 0;
}
