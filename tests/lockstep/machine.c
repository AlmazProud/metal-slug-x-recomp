#include "machine.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "neogeo.h"

static uint8_t *p;               /* P1 + P2 as seen by the 68000 (big-endian) */
static size_t p_size;
static uint8_t bios[0x20000];
static Machine *m;

static uint8_t *load(const char *path, uint8_t *dst, size_t *n)
{
    FILE *f = fopen(path, "rb");
    if (!f) { fprintf(stderr, "Could not read %s\n", path); exit(2); }
    if (!dst) {                  /* whole file */
        fseek(f, 0, SEEK_END);
        *n = (size_t)ftell(f);
        fseek(f, 0, SEEK_SET);
        dst = malloc(*n);
    }
    if (!dst || fread(dst, 1, *n, f) != *n) { fprintf(stderr, "Could not read %s\n", path); exit(2); }
    fclose(f);
    for (size_t i = 0; i < *n; i += 2) { uint8_t t = dst[i]; dst[i] = dst[i + 1]; dst[i + 1] = t; }
    return dst;
}

void mach_load_roms(const char *path_p, const char *path_bios)
{
    size_t n = sizeof bios;
    p = load(path_p, NULL, &p_size);
    load(path_bios, bios, &n);
}

void mach_init(Machine *mq, const int64_t *cycles)
{
    memset(mq, 0, sizeof *mq);
    mq->p2_base = 0x100000;      /* bank 0 (NEO-SMA: table offset 0) */
    mq->cycles = cycles;
}

/* ---------------------------------------------------------------- P2 banks */
static uint32_t altera_base(int b) { return (size_t)(b + 1) * 0x100000 < p_size ? (uint32_t)(b + 1) * 0x100000 : 0x100000; }

static uint32_t sma_base(uint16_t v)
{
    uint32_t n = 0;
    for (int i = 0; i < 6; i++) n |= (uint32_t)((v >> ng_game->sma_bank_bits[i]) & 1) << i;
    uint32_t base = 0x100000 + ng_game->sma_banks[n];
    return base + 0x100000 <= p_size ? base : 0x100000;
}

/* NEO-PVC (the port's bus.c, docs/PHASE2_HARDWARE.md): a word of the window at program byte o;
   with an odd offset it straddles two program words; past the end, 0xFFFF. */
static uint32_t p2_word(uint32_t o)
{
    if (!(o & 1)) return o + 1 < p_size ? (uint32_t)p[o] << 8 | p[o + 1] : 0xFFFF;
    uint32_t lo = (o ^ 1) < p_size ? p[o ^ 1] : 0xFF;
    uint32_t hi = ((o + 1) ^ 1) < p_size ? p[(o + 1) ^ 1] : 0xFF;
    return hi << 8 | lo;
}

/* After a write to 0x2FFFF0-3: the window shows P2 offset F2 << 16 | F3 << 8 | F0, then the chip
   rewrites the bytes. */
static void pvc_bank(void)
{
    uint8_t *r = m->cart_ram + 0x1FF0;
    m->p2_base = 0x100000 + ((uint32_t)r[2] << 16 | (uint32_t)r[3] << 8 | r[0]);
    r[0] &= 0xFE;
    r[1] = 0xA0;
    r[2] &= 0x7F;
}

void mach_use(Machine *mq) { m = mq; }

void mach_from_dump(Machine *mq, const uint8_t vectors[128], const uint8_t window[16], int64_t bank_value,
                    const uint8_t *ram, const uint8_t *sram, const uint8_t *palette,
                    const uint8_t *cart_ram)
{
    memcpy(mq->ram, ram, sizeof mq->ram);
    memcpy(mq->sram, sram, sizeof mq->sram);
    memcpy(mq->palette, palette, sizeof mq->palette);
    mq->cart_vectors = memcmp(vectors, bios, 128) != 0;
    mq->p2_base = 0x100000;
    if (ng_game->cart == NG_CART_PVC) {
        if (bank_value < 0 || !cart_ram) { fprintf(stderr, "dump: no NEO-PVC state\n"); exit(2); }
        mq->p2_base = 0x100000 + (uint32_t)bank_value;
        memcpy(mq->cart_ram, cart_ram, sizeof mq->cart_ram);
        for (int i = 0; i < 16; i += 2)
            if (p2_word(mq->p2_base + (uint32_t)i) != ((uint32_t)window[i] << 8 | window[i + 1])) {
                fprintf(stderr, "dump: NEO-PVC window does not match\n");
                exit(2);
            }
        return;
    }
    if (ng_game->cart == NG_CART_SMA && bank_value >= 0) {
        mq->p2_base = sma_base((uint16_t)bank_value);
    } else {
        for (int b = 0; b < 4; b++)
            if (memcmp(window, p + altera_base(b), 16) == 0) { mq->p2_base = altera_base(b); break; }
    }
    if (memcmp(window, p + mq->p2_base, 16) != 0) { fprintf(stderr, "dump: P2 bank not found\n"); exit(2); }
}

/* ---------------------------------------------------------------- protection (ROM_MAP.md) */
static uint16_t table_bit(uint32_t index)
{
    uint8_t byte = p[0xDEDD2 + ((index >> 3) & 0xFFF)];
    return (byte >> (~index & 7)) & 1;
}

static uint16_t prot_r(void)
{
    if (m->prot_cmd == 0x0001) return table_bit(m->prot_cnt++);
    if (m->prot_cmd == 0x0FFF) {
        uint32_t sel = ((uint32_t)m->ram[0xF00A] << 8 | m->ram[0xF00B]) - 1;
        return table_bit(sel);
    }
    return 0;
}

static void prot_w(uint32_t off, uint16_t v)
{
    switch (off) {
    case 0x0: m->prot_cmd = 0; break;
    case 0x2: case 0x4: m->prot_cmd |= v; break;
    case 0xA: m->prot_cnt = 0; m->prot_cmd = 0; break;
    default: break;
    }
}

/* ---------------------------------------------------------------- LSPC */
static uint16_t lspc_mode(void)
{
    /* 768 68000 cycles per line, 264 lines per frame; lines start at 0xF8. */
    int64_t c = *m->cycles;
    uint32_t line = (uint32_t)((c / 768) % 264);
    uint32_t anim = (uint32_t)((c / (768 * 264 * 8)) & 7);
    return (uint16_t)(((line + 0xF8) & 0x1FF) << 7 | anim);
}

/* ---------------------------------------------------------------- word access */
uint32_t mach_r16(uint32_t a)
{
    a &= 0xFFFFFE;
    if (a < 0x80 && !m->cart_vectors) return (uint32_t)bios[a] << 8 | bios[a + 1];
    if (a < 0x100000) return (uint32_t)p[a] << 8 | p[a + 1];
    if (a < 0x200000) { a &= 0xFFFF; return (uint32_t)m->ram[a] << 8 | m->ram[a + 1]; }
    if (a < 0x300000) {
        if (ng_game->cart == NG_CART_ALTERA_MSLUGX && a >= 0x2FFFE0 && a <= 0x2FFFEF) return prot_r();
        if (ng_game->cart == NG_CART_SMA && a == ng_game->sma_id_addr) return ng_game->sma_id;
        if (ng_game->cart == NG_CART_PVC) {
            if (a >= 0x2FE000) return (uint32_t)m->cart_ram[a & 0x1FFF] << 8 | m->cart_ram[(a & 0x1FFF) + 1];
            return p2_word(m->p2_base + (a - 0x200000));
        }
        uint32_t o = m->p2_base + (a - 0x200000);
        return o + 1 < p_size ? ((uint32_t)p[o] << 8 | p[o + 1]) : 0xFFFF;
    }
    if (a < 0x3C0000) return 0xFFFF;                     /* inputs: nothing pressed */
    if (a < 0x3E0000) {
        switch (a & 0xE) {
        case 0x0: case 0x2: return m->vram[m->vram_addr];
        case 0x4: return m->vram_mod;
        case 0x6: return lspc_mode();
        default: return 0xFFFF;
        }
    }
    if (a >= 0x400000 && a < 0x800000) { a &= 0x1FFF; return (uint32_t)m->palette[a] << 8 | m->palette[a + 1]; }
    if (a >= 0xC00000 && a < 0xD00000) { a &= 0x1FFFF; return (uint32_t)bios[a] << 8 | bios[a + 1]; }
    if (a >= 0xD00000 && a < 0xE00000) { a &= 0xFFFF; return (uint32_t)m->sram[a] << 8 | m->sram[a + 1]; }
    return 0xFFFF;
}

uint32_t mach_r8(uint32_t a)
{
    uint32_t w = mach_r16(a & ~1u);
    return (a & 1) ? (w & 0xFF) : (w >> 8);
}

void mach_w16(uint32_t a, uint32_t v)
{
    a &= 0xFFFFFE;
    v &= 0xFFFF;
    if (a >= 0x100000 && a < 0x200000) { a &= 0xFFFF; m->ram[a] = (uint8_t)(v >> 8); m->ram[a + 1] = (uint8_t)v; return; }
    if (ng_game->cart == NG_CART_PVC) {
        if (a >= 0x2FE000 && a < 0x300000) {
            m->cart_ram[a & 0x1FFF] = (uint8_t)(v >> 8);
            m->cart_ram[(a & 0x1FFF) + 1] = (uint8_t)v;
            if ((a & ~3u) == 0x2FFFF0) pvc_bank();
            return;
        }
    } else if (ng_game->cart == NG_CART_SMA) {
        if (a == ng_game->sma_bank_reg) m->p2_base = sma_base((uint16_t)v);
    } else {
        if (ng_game->cart == NG_CART_ALTERA_MSLUGX && a >= 0x2FFFE0 && a <= 0x2FFFEF) {
            prot_w(a - 0x2FFFE0, (uint16_t)v);
            return;
        }
        if (a >= 0x2FFFF0 && a <= 0x2FFFFF) { m->p2_base = altera_base(v & 7); return; }
    }
    if (a >= 0x3A0000 && a < 0x3C0000) {                 /* system latch (odd byte) */
        uint32_t off = (a & 0x1F) >> 1;
        if ((off & 7) == 1) m->cart_vectors = (off >> 3) & 1;
        return;
    }
    if (a >= 0x3C0000 && a < 0x3E0000) {
        switch (a & 0xE) {
        case 0x0: m->vram_addr = (uint16_t)v; break;
        case 0x2: m->vram[m->vram_addr] = (uint16_t)v; m->vram_addr = (uint16_t)(m->vram_addr + m->vram_mod); break;
        case 0x4: m->vram_mod = (uint16_t)v; break;
        default: break;
        }
        return;
    }
    if (a >= 0x400000 && a < 0x800000) { a &= 0x1FFF; m->palette[a] = (uint8_t)(v >> 8); m->palette[a + 1] = (uint8_t)v; return; }
    if (a >= 0xD00000 && a < 0xE00000) { a &= 0xFFFF; m->sram[a] = (uint8_t)(v >> 8); m->sram[a + 1] = (uint8_t)v; return; }
    /* everything else (watchdog, Z80, etc.): no effect in this model */
}

void mach_w8(uint32_t a, uint32_t v)
{
    v &= 0xFF;
    uint32_t d = a & 0xFFFFFF;
    if (d >= 0x100000 && d < 0x200000) { m->ram[d & 0xFFFF] = (uint8_t)v; return; }
    if (d >= 0x400000 && d < 0x800000) { m->palette[d & 0x1FFF] = (uint8_t)v; return; }
    if (d >= 0xD00000 && d < 0xE00000) { m->sram[d & 0xFFFF] = (uint8_t)v; return; }
    if (ng_game->cart == NG_CART_PVC && d >= 0x2FE000 && d < 0x300000) {
        m->cart_ram[d & 0x1FFF] = (uint8_t)v;
        if ((d & ~3u) == 0x2FFFF0) pvc_bank();
        return;
    }
    /* Registers: the 68000 puts the byte on both halves of the data bus. */
    mach_w16(d & ~1u, v << 8 | v);
}
