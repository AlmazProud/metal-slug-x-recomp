#include "test_memory.h"

#include <stdio.h>
#include <stdlib.h>

static uint8_t p1[0x100000];
static uint8_t bios[0x20000];
static uint32_t semilla_actual;
static Layer *capa_actual;

static void cargar(const char *ruta, uint8_t *dst, size_t n)
{
    FILE *f = fopen(ruta, "rb");
    if (!f || fread(dst, 1, n, f) != n) {
        fprintf(stderr, "No se pudo leer %s\n", ruta);
        exit(2);
    }
    fclose(f);
    for (size_t i = 0; i < n; i += 2) {   /* orden de archivo de MAME -> big-endian del 68000 */
        uint8_t t = dst[i]; dst[i] = dst[i + 1]; dst[i + 1] = t;
    }
}

void mem_load_roms(const char *path_p, const char *path_bios)
{
    cargar(path_p, p1, sizeof p1);
    cargar(path_bios, bios, sizeof bios);
}

void mem_seed(uint32_t seed) { semilla_actual = seed; }
void mem_use_layer(Layer *layer) { capa_actual = layer; }

static uint8_t base(uint32_t a)
{
    a &= 0xFFFFFF;
    if (a < 0x100000) return p1[a];
    if (a >= 0xC00000 && a < 0xC20000) return bios[a - 0xC00000];
    uint32_t h = (a ^ semilla_actual) * 0x9E3779B1u;
    h ^= h >> 15; h *= 0x85EBCA77u; h ^= h >> 13;
    return (uint8_t)h;
}

uint8_t mem_read8_in(const Layer *layer, uint32_t a)
{
    a &= 0xFFFFFF;
    for (int i = layer->n - 1; i >= 0; i--)
        if (layer->dir[i] == a) return layer->val[i];
    return base(a);
}

uint8_t mem_read8(uint32_t a) { return mem_read8_in(capa_actual, a); }

void mem_write8(uint32_t a, uint8_t v)
{
    a &= 0xFFFFFF;
    if (capa_actual->n >= MAX_WRITES) {
        fprintf(stderr, "Demasiadas escrituras en una prueba\n");
        exit(3);
    }
    capa_actual->dir[capa_actual->n] = a;
    capa_actual->val[capa_actual->n] = v;
    capa_actual->n++;
}

/* Bus del código recompilado (cpu68k.h). */
uint32_t bus_r8(uint32_t a)  { return mem_read8(a); }
uint32_t bus_r16(uint32_t a) { return (uint32_t)mem_read8(a) << 8 | mem_read8(a + 1); }
void bus_w8(uint32_t a, uint32_t v)  { mem_write8(a, (uint8_t)v); }
void bus_w16(uint32_t a, uint32_t v) { mem_write8(a, (uint8_t)(v >> 8)); mem_write8(a + 1, (uint8_t)v); }
