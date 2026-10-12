/* Memoria del arnés diferencial: una función base determinista (P1 y BIOS del usuario, el resto
 * pseudoaleatorio según una seed) más una layer de escrituras por cada CPU comparada. */
#ifndef TEST_MEMORY_H
#define TEST_MEMORY_H

#include <stdint.h>

#define MAX_WRITES 4096

typedef struct {
    uint32_t dir[MAX_WRITES];
    uint8_t val[MAX_WRITES];
    int n;
} Layer;

void mem_load_roms(const char *path_p, const char *path_bios);
void mem_seed(uint32_t seed);
void mem_use_layer(Layer *layer);       /* layer active para lecturas y escrituras */
uint8_t mem_read8(uint32_t a);
void mem_write8(uint32_t a, uint8_t v);
uint8_t mem_read8_in(const Layer *layer, uint32_t a);

#endif
