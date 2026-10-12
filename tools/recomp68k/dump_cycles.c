/* Dumps Musashi's base cycles for the 68000 (65536 opcodes, one byte each).
 * The recompiler's generator uses this table as the base cost of each instruction; the
 * dynamic adjustments (branches not taken, shifts, MOVEM, MUL, exceptions) are emitted by
 * the generator itself. Usage: dump_cycles <output.bin> */
#include <stdio.h>
#include "m68k.h"

extern unsigned char m68ki_cycles[][0x10000];

/* Musashi requires these functions even though no code is executed here. */
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
    if (argc < 2) { fprintf(stderr, "usage: dump_cycles <output.bin>\n"); return 2; }
    m68k_init();
    FILE *f = fopen(argv[1], "wb");
    if (!f || fwrite(m68ki_cycles[0], 1, 0x10000, f) != 0x10000) { fprintf(stderr, "write error\n"); return 1; }
    fclose(f);
    return 0;
}
