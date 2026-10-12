/* Máquina Neo Geo mínima y determinista para la prueba de lockstep del recompilador.
 *
 * NO es el runtime del port (Fase 3): solo modela lo necesario para que el BIOS y el juego
 * ejecuten código de forma reproducible: RAM, SRAM de respaldo, palette, VRAM, bancos de P2,
 * protección de mslugx, conmutación de vectors BIOS/cartucho y un contador de línea derivado
 * de los cycles. Las inputs leen "nada pulsado". Sin interrupciones.
 * Referencia del mapa de memoria: MAME 0.289 (src/mame/snk/neogeo.cpp), solo documentación. */
#ifndef MACHINE_H
#define MACHINE_H

#include <stdint.h>

typedef struct {
    uint8_t ram[0x10000];
    uint8_t sram[0x10000];
    uint8_t palette[0x2000];
    uint16_t vram[0x10000];
    uint16_t vram_addr, vram_mod;
    int bank;                  /* bank de P2 visible en 0x200000 */
    int cart_vectors;      /* 0 = vectors del BIOS en 0x000000 (estado de encendido) */
    uint16_t prot_cmd, prot_cnt;/* protección de mslugx */
    const int64_t *cycles;      /* reloj del CPU que está ejecutando */
} Machine;

void mach_load_roms(const char *path_p, const char *path_bios);
void mach_init(Machine *m, const int64_t *cycles);
void mach_use(Machine *m);
/* State inicial desde un volcado de MAME (tools/mame/dump_state.lua). */
void mach_from_dump(Machine *m, const uint8_t vectors[128], const uint8_t window[16],
                       const uint8_t *ram, const uint8_t *sram, const uint8_t *palette);

uint32_t mach_r8(uint32_t a);
uint32_t mach_r16(uint32_t a);
void mach_w8(uint32_t a, uint32_t v);
void mach_w16(uint32_t a, uint32_t v);

#endif
