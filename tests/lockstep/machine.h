/* Minimal, deterministic Neo Geo machine for the recompiler's lockstep test.
 *
 * It is NOT the port's runtime (Phase 3): it only models what the BIOS and the game need to
 * run code reproducibly: RAM, backup SRAM, palette, VRAM, P2 banks, mslugx protection,
 * BIOS/cartridge vector switching and a line counter derived from the cycles. Inputs read
 * "nothing pressed". No interrupts.
 * Memory map reference: MAME 0.289 (src/mame/snk/neogeo.cpp), documentation only. */
#ifndef MACHINE_H
#define MACHINE_H

#include <stdint.h>

typedef struct {
    uint8_t ram[0x10000];
    uint8_t sram[0x10000];
    uint8_t palette[0x2000];
    uint16_t vram[0x10000];
    uint16_t vram_addr, vram_mod;
    int bank;                  /* P2 bank visible at 0x200000 */
    int cart_vectors;          /* 0 = BIOS vectors at 0x000000 (power-on state) */
    uint16_t prot_cmd, prot_cnt;/* mslugx protection */
    const int64_t *cycles;     /* clock of the CPU currently running */
} Machine;

void mach_load_roms(const char *path_p, const char *path_bios);
void mach_init(Machine *m, const int64_t *cycles);
void mach_use(Machine *m);
/* Initial state from a MAME dump (tools/mame/dump_state.lua). */
void mach_from_dump(Machine *m, const uint8_t vectors[128], const uint8_t window[16],
                    const uint8_t *ram, const uint8_t *sram, const uint8_t *palette);

uint32_t mach_r8(uint32_t a);
uint32_t mach_r16(uint32_t a);
void mach_w8(uint32_t a, uint32_t v);
void mach_w16(uint32_t a, uint32_t v);

#endif
