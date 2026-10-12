#include "test_memory.h"

#include <stdio.h>
#include <stdlib.h>

static uint8_t p1[0x100000];
static uint8_t bios[0x20000];
static uint32_t current_seed;
static Layer *current_layer;

static void load(const char *path, uint8_t *dst, size_t n)
{
    FILE *f = fopen(path, "rb");
    if (!f || fread(dst, 1, n, f) != n) {
        fprintf(stderr, "Could not read %s\n", path);
        exit(2);
    }
    fclose(f);
    for (size_t i = 0; i < n; i += 2) {   /* MAME file order -> 68000 big-endian */
        uint8_t t = dst[i]; dst[i] = dst[i + 1]; dst[i + 1] = t;
    }
}

void mem_load_roms(const char *path_p, const char *path_bios)
{
    load(path_p, p1, sizeof p1);
    load(path_bios, bios, sizeof bios);
}

void mem_seed(uint32_t seed) { current_seed = seed; }
void mem_use_layer(Layer *layer) { current_layer = layer; }

static uint8_t base(uint32_t a)
{
    a &= 0xFFFFFF;
    if (a < 0x100000) return p1[a];
    if (a >= 0xC00000 && a < 0xC20000) return bios[a - 0xC00000];
    uint32_t h = (a ^ current_seed) * 0x9E3779B1u;
    h ^= h >> 15; h *= 0x85EBCA77u; h ^= h >> 13;
    return (uint8_t)h;
}

uint8_t mem_read8_in(const Layer *layer, uint32_t a)
{
    a &= 0xFFFFFF;
    for (int i = layer->n - 1; i >= 0; i--)
        if (layer->addr[i] == a) return layer->val[i];
    return base(a);
}

uint8_t mem_read8(uint32_t a) { return mem_read8_in(current_layer, a); }

void mem_write8(uint32_t a, uint8_t v)
{
    a &= 0xFFFFFF;
    if (current_layer->n >= MAX_WRITES) {
        fprintf(stderr, "Too many writes in one test\n");
        exit(3);
    }
    current_layer->addr[current_layer->n] = a;
    current_layer->val[current_layer->n] = v;
    current_layer->n++;
}

/* Bus of the recompiled code (cpu68k.h). */
uint32_t bus_r8(uint32_t a)  { return mem_read8(a); }
uint32_t bus_r16(uint32_t a) { return (uint32_t)mem_read8(a) << 8 | mem_read8(a + 1); }
void bus_w8(uint32_t a, uint32_t v)  { mem_write8(a, (uint8_t)v); }
void bus_w16(uint32_t a, uint32_t v) { mem_write8(a, (uint8_t)(v >> 8)); mem_write8(a + 1, (uint8_t)v); }
