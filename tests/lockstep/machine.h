/* Máquina Neo Geo mínima y determinista para la prueba de lockstep del recompilador.
 *
 * NO es el runtime del port (Fase 3): solo modela lo necesario para que el BIOS y el juego
 * ejecuten código de forma reproducible: RAM, SRAM de respaldo, paleta, VRAM, bancos de P2,
 * protección de mslugx, conmutación de vectores BIOS/cartucho y un contador de línea derivado
 * de los ciclos. Las entradas leen "nada pulsado". Sin interrupciones.
 * Referencia del mapa de memoria: MAME 0.289 (src/mame/snk/neogeo.cpp), solo documentación. */
#ifndef MAQUINA_H
#define MAQUINA_H

#include <stdint.h>

typedef struct {
    uint8_t ram[0x10000];
    uint8_t sram[0x10000];
    uint8_t paleta[0x2000];
    uint16_t vram[0x10000];
    uint16_t vram_dir, vram_mod;
    int banco;                  /* banco de P2 visible en 0x200000 */
    int vectores_cartucho;      /* 0 = vectores del BIOS en 0x000000 (estado de encendido) */
    uint16_t prot_cmd, prot_cnt;/* protección de mslugx */
    const int64_t *ciclos;      /* reloj del CPU que está ejecutando */
} Maquina;

void maq_cargar_roms(const char *ruta_p, const char *ruta_bios);
void maq_iniciar(Maquina *m, const int64_t *ciclos);
void maq_usar(Maquina *m);
/* Estado inicial desde un volcado de MAME (tools/mame/dump_state.lua). */
void maq_desde_volcado(Maquina *m, const uint8_t vectores[128], const uint8_t ventana[16],
                       const uint8_t *ram, const uint8_t *sram, const uint8_t *paleta);

uint32_t maq_r8(uint32_t a);
uint32_t maq_r16(uint32_t a);
void maq_w8(uint32_t a, uint32_t v);
void maq_w16(uint32_t a, uint32_t v);

#endif
