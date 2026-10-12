/* El código recompilado y Musashi (referencia y respaldo) usan el mismo bus: la máquina activa. */
#include "maquina.h"

uint32_t bus_r8(uint32_t a)  { return maq_r8(a); }
uint32_t bus_r16(uint32_t a) { return maq_r16(a); }
void bus_w8(uint32_t a, uint32_t v)  { maq_w8(a, v); }
void bus_w16(uint32_t a, uint32_t v) { maq_w16(a, v); }

unsigned int m68k_read_memory_8(unsigned int a)  { return maq_r8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return maq_r16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return maq_r16(a) << 16 | maq_r16(a + 2); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return maq_r16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return m68k_read_memory_32(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v)  { maq_w8(a, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { maq_w16(a, v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { maq_w16(a, v >> 16); maq_w16(a + 2, v & 0xFFFF); }
