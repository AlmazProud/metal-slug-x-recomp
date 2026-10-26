/* 68000 memory map on the Neo Geo MVS (the cartridge type comes from ng_game->cart).
 *
 *   000000-00007F  vectors: BIOS or cartridge depending on the system latch
 *   000080-0FFFFF  P1
 *   100000-1FFFFF  work RAM (64 KB, mirrored)
 *   200000-2FFFFF  P2 window.
 *                  Metal Slug X (NG_CART_ALTERA_MSLUGX): bank selected by writing to
 *                  2FFFF0-2FFFFF; 2FFFE0-2FFFEF: ALTERA protection (Metal Slug X port,
 *                  docs/ROM_MAP.md there).
 *                  NEO-SMA (NG_CART_SMA): bank selected by writing to ng_game->sma_bank_reg,
 *                  chip ID at ng_game->sma_id_addr (docs/ROM_MAP.md).
 *                  Standard (NG_CART_STANDARD, Metal Slug 4): as Metal Slug X's bank select,
 *                  without the protection (learned from MAME: values 0-3 select P2's banks,
 *                  every other value bank 0; docs/PHASE2_HARDWARE.md).
 *                  NEO-PVC (NG_CART_PVC): 2FE000-2FFFFF is 8 KB of RAM; writing its bytes
 *                  2FFFF0-2FFFF3 selects the window at any byte offset (docs/ROM_MAP.md,
 *                  docs/PHASE2_HARDWARE.md).
 *   300000         P1 (high byte) / DIP (low byte); 300001 (write) = watchdog
 *   300080         TEST (low byte)
 *   320000         Z80 reply (high byte) / coins and RTC (low byte); write = command
 *   340000         P2
 *   380000         START/SELECT; writes = I/O control (RTC at 380050/51)
 *   3A0000-3A001F  system latch (odd bytes)
 *   3C0000-3C000F  LSPC registers
 *   400000-7FFFFF  palette (8 KB, mirrored; two banks)
 *   C00000-CFFFFF  BIOS (128 KB, mirrored)
 *   D00000-DFFFFF  backup SRAM (64 KB, mirrored, write-protected)
 *
 * Unmapped reads return 0xFFFF (on real hardware it is the last value on the bus, almost
 * always the prefetch); they are counted in ng.unmapped_reads.
 */
#include "neogeo.h"

#include "m68k.h"

#define WATCHDOG_CYCLES 1622015          /* 3 244 030 cycles of the 24 MHz master clock */

/* ------------------------------------------------------------------ Metal Slug X ALTERA protection */
static uint16_t table_bit(uint32_t index)
{
    uint8_t byte = ng_roms.p[0xDEDD2 + ((index >> 3) & 0xFFF)];
    return (byte >> (~index & 7)) & 1;
}

static uint16_t prot_read(void)
{
    if (ng.prot_cmd == 0x0001) return table_bit(ng.prot_cnt++);
    if (ng.prot_cmd == 0x0FFF) {
        uint32_t sel = ((uint32_t)ng.ram[0xF00A] << 8 | ng.ram[0xF00B]) - 1;
        return table_bit(sel);
    }
    return 0;
}

static void prot_write(uint32_t off, uint16_t v)
{
    switch (off) {
    case 0x0: ng.prot_cmd = 0; break;
    case 0x2: case 0x4: ng.prot_cmd |= v; break;
    case 0xA: ng.prot_cnt = 0; ng.prot_cmd = 0; break;
    default: break;
    }
}

static void p2_bank(uint16_t v)
{
    uint32_t b = v & 7;
    if ((b + 1) * 0x100000 >= ng_roms.p_size) b = 0;
    ng.p2_base = (b + 1) * 0x100000;
}

/* ------------------------------------------------------------------ NEO-SMA */
static void sma_bank(uint16_t v)
{
    uint32_t n = 0;
    for (int i = 0; i < 6; i++) n |= (uint32_t)((v >> ng_game->sma_bank_bits[i]) & 1) << i;
    uint32_t base = 0x100000 + ng_game->sma_banks[n];
    ng.p2_base = base + 0x100000 <= ng_roms.p_size ? base : 0x100000;
}

/* ------------------------------------------------------------------ NEO-PVC */
/* Learned from MAME as a black box (tools/bus_probe.py): a write to 2FFFF0-2FFFF3 stores its
   bytes, then the window starts at program byte 0x100000 + (F2 << 16 | F3 << 8 | F0) (the bytes
   at 2FFFF2, 2FFFF3, 2FFFF0 as they are), and the chip rewrites them: F0 loses bit 0, F1 reads
   0xA0, F2 loses bit 7. */
static void pvc_bank(void)
{
    uint8_t *r = ng.cart_ram + 0x1FF0;
    ng.p2_base = 0x100000 + ((uint32_t)r[2] << 16 | (uint32_t)r[3] << 8 | r[0]);
    r[0] &= 0xFE;
    r[1] = 0xA0;
    r[2] &= 0x7F;
}

/* A word of the P2 window at program byte o. With an odd bank offset the word straddles two of
   the program's words: MAME reads the region, little-endian 16-bit words, at the odd offset.
   Past the end of the program: 0xFFFF. */
static uint32_t p2_word(uint32_t o)
{
    const uint8_t *p = ng_roms.p;
    if (!(o & 1)) return o + 1 < ng_roms.p_size ? (uint32_t)p[o] << 8 | p[o + 1] : 0xFFFF;
    uint32_t lo = (o ^ 1) < ng_roms.p_size ? p[o ^ 1] : 0xFF;
    uint32_t hi = ((o + 1) ^ 1) < ng_roms.p_size ? p[(o + 1) ^ 1] : 0xFF;
    return hi << 8 | lo;
}

/* ------------------------------------------------------------------ system latch */
static void latch(uint32_t a)
{
    uint32_t off = (a & 0x1F) >> 1;
    uint8_t v = (off >> 3) & 1;
    switch (off & 7) {
    case 0: ng.shadow = v; break;
    case 1: ng.cart_vectors = v; break;
    case 5: ng.cart_fix = v; break;             /* also the Z80 ROM (BIOS/cartridge) */
    case 6: ng.sram_unlocked = v; break;
    case 7: ng.palette_bank = v; break;
    default: break;                                  /* 2-4: memory card */
    }
}

/* ------------------------------------------------------------------ I/O */
static uint16_t io_read_raw(uint32_t a);
static int64_t write_end(void);

static uint16_t io_read(uint32_t a)
{
    if (ng_trace_reads && ng_cpu.cycles >= ng_trace_from && ng_cpu.cycles <= ng_trace_to &&
        (a & 0x0E0000) != 0x020000 && (a & 0x0E0000) <= 0x080000) {
        uint16_t v = io_read_raw(a);
        fprintf(ng_trace_reads, "%.3f pc=%06X %06X %04X\n", ng_cpu.cycles / 12.0, ng_cpu.pc, a, v);
        return v;
    }
    return io_read_raw(a);
}

static uint16_t io_read_raw(uint32_t a)
{
    if (ng_before_input_read) ng_before_input_read();
    switch (a & 0x0E0000) {
    case 0x000000:                                   /* 300000-31FFFF */
        if (a & 0x80) return 0xFF00 | (ng.raw ? ng.raw_test : ng.test);
        return (uint16_t)((ng.raw ? ng.raw_p1 : (uint8_t)~ng.joy[0]) << 8 | ng.dsw);
    case 0x020000: {                                 /* 320000-33FFFF */
        if (ng.raw)
            return (uint16_t)(ng_sound_reply() << 8 | (ng.raw_coins & 0x3F) | ng_rtc_tp() << 6 | ng_rtc_data() << 7);
        uint8_t coins = 0x1F;
        if (ng.system[0] & NG_COIN) coins &= ~0x01;
        if (ng.system[1] & NG_COIN) coins &= ~0x02;
        if (ng.service) coins &= ~0x04;
        return (uint16_t)(ng_sound_reply() << 8 | coins | ng_rtc_tp() << 6 | ng_rtc_data() << 7);
    }
    case 0x040000:                                   /* 340000-35FFFF */
        return (uint16_t)((ng.raw ? ng.raw_p2 : (uint8_t)~ng.joy[1]) << 8 | 0xFF);
    case 0x080000: {                                 /* 380000-39FFFF */
        if (ng.raw) return (uint16_t)(ng.raw_system << 8 | 0xFF);
        uint8_t s = 0xFF;
        if (ng.system[0] & NG_START) s &= ~0x01;
        if (ng.system[0] & NG_SELECT) s &= ~0x02;
        if (ng.system[1] & NG_START) s &= ~0x04;
        if (ng.system[1] & NG_SELECT) s &= ~0x08;
        return (uint16_t)(s << 8 | 0xFF);
    }
    case 0x0C0000:                                   /* 3C0000-3DFFFF */
        return ng_video_read((a >> 1) & 3);
    default:
        ng.unmapped_reads++;
        return 0xFFFF;
    }
}

/* I/O write. `mask`: 0xFF00 even byte, 0x00FF odd byte, 0xFFFF word. */
/* Trace of I/O and palette writes (same format as tools/mame/trace_io.lua). */
static void trace_write(uint32_t a, uint16_t v, uint16_t mask)
{
    if (ng_trace_io && ng_cpu.cycles >= ng_trace_from && ng_cpu.cycles <= ng_trace_to)
        fprintf(ng_trace_io, "%.3f pc=%06X %06X %04X/%04X\n", ng_cpu.cycles / 12.0, ng_cpu.pc, a, v & mask, mask);
}

static void io_write(uint32_t a, uint16_t v, uint16_t mask)
{
    trace_write(a, v, mask);
    switch (a & 0x0E0000) {
    case 0x000000:
        /* the timeout counts from the write access (it starts 4 cycles before its end), as
           for video writes (order_video_write); checked against MAME's watchdog reset */
        if (mask & 0x00FF) ng.watchdog = write_end() - 4 + WATCHDOG_CYCLES;
        return;
    case 0x020000:
        if (mask & 0xFF00) ng_sound_command((uint8_t)(v >> 8), write_end() - 3);
        return;
    case 0x080000:                                   /* I/O control (odd bytes) */
        if (mask & 0x00FF) {
            uint32_t off = (a & 0xFF) >> 1;
            if ((off & 0x78) == 0x28) ng_rtc_write((uint8_t)v);
            /* 0x00 controller select, 0x08 card bank, 0x10 slot, 0x18/0x20 LEDs,
               0x30/0x70 coin counters: no effect in the port */
        }
        return;
    case 0x0A0000:                                   /* system latch (odd bytes) */
        if (mask & 0x00FF) latch(a);
        return;
    case 0x0C0000:
        /* The LSPC ignores low-byte-only accesses; on high-byte-only accesses it copies the
           byte into both halves. */
        if (mask == 0x00FF) return;
        if (mask == 0xFF00) v = (uint16_t)((v & 0xFF00) | v >> 8);
        ng_video_write((a >> 1) & 7, v);
        return;
    default:
        ng.unmapped_writes++;
        return;
    }
}

/* ------------------------------------------------------------------ access */
uint32_t bus_r16(uint32_t a)
{
    a &= 0xFFFFFE;
    switch (a >> 20) {
    case 0x0:
        if (a < 0x80 && !ng.cart_vectors) return (uint32_t)ng_roms.bios[a] << 8 | ng_roms.bios[a + 1];
        return (uint32_t)ng_roms.p[a] << 8 | ng_roms.p[a + 1];
    case 0x1: {
        uint32_t v = (uint32_t)ng.ram[a & 0xFFFF] << 8 | ng.ram[(a & 0xFFFF) + 1];
        for (int i = 0; i < NG_FORCED_COUNT; i++) {
            const NgForcedRead *f = &ng_forced_read[i];
            if (f->active && a == f->addr && ng_cpu.pc < 0xC00000 &&
                (!f->pc_last || (ng_cpu.pc >= f->pc_first && ng_cpu.pc <= f->pc_last)))
                v = (v & ~(uint32_t)f->mask) | f->value;
        }
        return v;
    }
    case 0x2: {
        if (ng_game->cart == NG_CART_ALTERA_MSLUGX && a >= 0x2FFFE0 && a <= 0x2FFFEF) return prot_read();
        if (ng_game->cart == NG_CART_SMA && a == ng_game->sma_id_addr) return ng_game->sma_id;
        if (ng_game->cart == NG_CART_PVC) {
            if (a >= 0x2FE000) return (uint32_t)ng.cart_ram[a & 0x1FFF] << 8 | ng.cart_ram[(a & 0x1FFF) + 1];
            return p2_word(ng.p2_base + (a & 0xFFFFF));
        }
        uint32_t o = ng.p2_base + (a & 0xFFFFF);
        return (uint32_t)ng_roms.p[o] << 8 | ng_roms.p[o + 1];
    }
    case 0x3:
        return io_read(a);
    case 0x4: case 0x5: case 0x6: case 0x7:
        return ng.palette[(uint32_t)ng.palette_bank << 12 | (a & 0x1FFF) >> 1];
    case 0xC:
        a &= 0x1FFFF;
        return (uint32_t)ng_roms.bios[a] << 8 | ng_roms.bios[a + 1];
    case 0xD:
        a &= 0xFFFF;
        return (uint32_t)ng.sram[a] << 8 | ng.sram[a + 1];
    default:                                         /* 800000-BFFFFF card (absent), E-F */
        ng.unmapped_reads++;
        return 0xFFFF;
    }
}

uint32_t bus_r8(uint32_t a)
{
    uint32_t w = bus_r16(a & ~1u);
    return (a & 1) ? (w & 0xFF) : (w >> 8);
}

/* End of the bus access of a write within the current instruction (ng_cpu.op, which started
 * at ng_cpu.start_cycle and ends at ng_cpu.cycles). Per the 68000 microcode: in MOVE the write
 * comes before the last prefetch (it ends 4 cycles before the end); in the rest
 * (read-modify-write, CLR...) it is the last access. 32-bit writes are two consecutive
 * writes. MOVEM to memory: after the prefetch of the register mask (and of the address
 * extension words, and d8(An,Xn)'s 2 internal cycles), one write every 4 cycles, then the last
 * prefetch: (An) / -(An) 8+4n cycles, d16(An) and abs.W 12+4n, d8(An,Xn) 14+4n, abs.L 16+4n.
 * Counted from the instruction's start, so the interpreter and the recompiled code agree (Metal
 * Slug writes the LSPC with MOVEM to abs.L, docs/PHASE3_RECOMP.md). */
static int64_t write_end(void)
{
    static int64_t start_cycle = -1;
    static int n;
    if (ng_cpu.start_cycle != start_cycle) { start_cycle = ng_cpu.start_cycle; n = 0; }
    n++;
    uint32_t op = ng_cpu.op;
    if ((op & 0xFF80) == 0x4880 && ((op >> 3) & 7) >= 2) {    /* MOVEM <list>,<memory> */
        int mode = (op >> 3) & 7, reg = op & 7;
        int ea = mode <= 4 ? 0 : mode == 5 ? 4 : mode == 6 ? 6 : reg == 0 ? 4 : 8;
        return ng_cpu.start_cycle + 4 + ea + 4 * n;
    }
    int move = (op & 0xC000) == 0 && (op & 0x3000) != 0;
    int is_long = move ? (op & 0x3000) == 0x2000 : ((op >> 6) & 3) == 2;
    int64_t end = ng_cpu.cycles - (move ? 4 : 0);
    return (is_long && n == 1) ? end - 4 : end;
}

/* MAME processes an event at T when the CPU stops at the first access end >= T: if an access
 * before this write already ended at T or later, the event (e.g. drawing a line) happens
 * before the write. */
static void order_video_write(void)
{
    int64_t te = write_end();
    if (ng_next_limit() <= te - 4) ng_catch_up(te - 4);
}

uint32_t ng_watch_write;
uint32_t ng_hook_word;
void (*ng_on_hook_write)(uint16_t before, uint16_t after);

static void write_bus(uint32_t a, uint16_t v, uint16_t mask)
{
    a &= 0xFFFFFE;
    if (a == ng_watch_write)
        printf("W f=%llu pc=%06X a=%06X v=%04X mask=%04X\n", (unsigned long long)ng.frames, ng_cpu.pc, a, v, mask);
    if ((a >= 0x3A0000 && a < 0x3E0000) || (a >= 0x400000 && a < 0x800000)) order_video_write();
    switch (a >> 20) {
    case 0x1: {
        uint32_t full = a;
        a &= 0xFFFF;
        uint16_t before = (uint16_t)(ng.ram[a] << 8 | ng.ram[a + 1]);
        if (mask & 0xFF00) ng.ram[a] = (uint8_t)(v >> 8);
        if (mask & 0x00FF) ng.ram[a + 1] = (uint8_t)v;
        if (full == ng_hook_word && ng_on_hook_write)
            ng_on_hook_write(before, (uint16_t)(ng.ram[a] << 8 | ng.ram[a + 1]));
        return;
    }
    case 0x2:
        if (ng_game->cart == NG_CART_ALTERA_MSLUGX) {
            if (a >= 0x2FFFE0 && a <= 0x2FFFEF) { prot_write(a - 0x2FFFE0, v); return; }
            if (a >= 0x2FFFF0) { p2_bank(v); return; }
        }
        if (ng_game->cart == NG_CART_STANDARD && a >= 0x2FFFF0) { p2_bank(v); return; }
        if (ng_game->cart == NG_CART_SMA && a == ng_game->sma_bank_reg) { sma_bank(v); return; }
        if (ng_game->cart == NG_CART_PVC && a >= 0x2FE000) {
            uint8_t *r = ng.cart_ram + (a & 0x1FFF);
            if (mask & 0xFF00) r[0] = (uint8_t)(v >> 8);
            if (mask & 0x00FF) r[1] = (uint8_t)v;
            if ((a & ~3u) == 0x2FFFF0) pvc_bank();
            return;
        }
        ng.unmapped_writes++;
        return;
    case 0x3:
        io_write(a, v, mask);
        return;
    case 0x4: case 0x5: case 0x6: case 0x7: {
        trace_write(a, v, mask);
        uint32_t i = (uint32_t)ng.palette_bank << 12 | (a & 0x1FFF) >> 1;
        uint16_t new_value = (uint16_t)((ng.palette[i] & ~mask) | (v & mask));
        ng_palette_write(i, new_value);
        return;
    }
    case 0xD:
        if (!ng.sram_unlocked) return;
        a &= 0xFFFF;
        if (mask & 0xFF00) ng.sram[a] = (uint8_t)(v >> 8);
        if (mask & 0x00FF) ng.sram[a + 1] = (uint8_t)v;
        return;
    default:
        ng.unmapped_writes++;
        return;
    }
}

void bus_w16(uint32_t a, uint32_t v) { write_bus(a, (uint16_t)v, 0xFFFF); }

void bus_w8(uint32_t a, uint32_t v)
{
    /* The 68000 puts the byte on both halves of the data bus; UDS/LDS select the half. */
    v &= 0xFF;
    write_bus(a, (uint16_t)(v << 8 | v), (a & 1) ? 0x00FF : 0xFF00);
}

/* ------------------------------------------------------------------ Musashi (fallback) */
unsigned int m68k_read_memory_8(unsigned int a)  { return bus_r8(a); }
unsigned int m68k_read_memory_16(unsigned int a) { return bus_r16(a); }
unsigned int m68k_read_memory_32(unsigned int a) { return bus_r16(a) << 16 | bus_r16(a + 2); }
unsigned int m68k_read_disassembler_16(unsigned int a) { return bus_r16(a); }
unsigned int m68k_read_disassembler_32(unsigned int a) { return m68k_read_memory_32(a); }
void m68k_write_memory_8(unsigned int a, unsigned int v)  { bus_w8(a, v); }
void m68k_write_memory_16(unsigned int a, unsigned int v) { bus_w16(a, v); }
void m68k_write_memory_32(unsigned int a, unsigned int v) { bus_w16(a, v >> 16); bus_w16(a + 2, v & 0xFFFF); }

void ng_bus_init(void)
{
    ng.p2_base = 0x100000;               /* bank 0 (NEO-SMA: table offset 0) */
    ng.prot_cmd = ng.prot_cnt = 0;
    ng.shadow = ng.cart_vectors = ng.cart_fix = ng.sram_unlocked = ng.palette_bank = 0;
    ng.dsw = 0xFF;
    ng.test = 0x80;                  /* bit 7: test button not pressed; bit 6: MVS */
    ng.watchdog = ng_cpu.cycles + WATCHDOG_CYCLES;
}
