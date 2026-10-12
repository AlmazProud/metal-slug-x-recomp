/* Memoria del arnés diferencial: una función base determinista (P1 y BIOS del usuario, el resto
 * pseudoaleatorio según una semilla) más una capa de escrituras por cada CPU comparada. */
#ifndef MEMORIA_PRUEBA_H
#define MEMORIA_PRUEBA_H

#include <stdint.h>

#define MAX_ESCRITURAS 4096

typedef struct {
    uint32_t dir[MAX_ESCRITURAS];
    uint8_t val[MAX_ESCRITURAS];
    int n;
} Capa;

void mem_cargar_roms(const char *ruta_p, const char *ruta_bios);
void mem_semilla(uint32_t semilla);
void mem_usar_capa(Capa *capa);       /* capa activa para lecturas y escrituras */
uint8_t mem_leer8(uint32_t a);
void mem_escribir8(uint32_t a, uint8_t v);
uint8_t mem_leer8_en(const Capa *capa, uint32_t a);

#endif
