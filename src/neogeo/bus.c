/* Mapa de memoria del 68000 en la Neo Geo MVS (con el cartucho de mslugx).
 *
 *   000000-00007F  vectors: BIOS o cartucho según el latch del system
 *   000080-0FFFFF  P1
 *   100000-1FFFFF  RAM de trabajo (64 KB, espejada)
 *   200000-2FFFFF  window de P2 (bank elegido escribiendo en 2FFFF0-2FFFFF);
 *                  2FFFE0-2FFFEF: protección ALTERA de mslugx (docs/ROM_MAP.md)
 *   300000         P1 (byte alto) / DIP (byte bajo); 300001 (escritura) = watchdog
 *   300080         TEST (byte bajo)
 *   320000         respuesta del Z80 (byte alto) / monedas y RTC (byte bajo); escritura = command
 *   340000         P2
 *   380000         START/SELECT; escrituras = control de E/S (RTC en 380050/51)
 *   3A0000-3A001F  latch del system (bytes impares)
 *   3C0000-3C000F  registros del LSPC
 *   400000-7FFFFF  palette (8 KB, espejada; dos bancos)
 *   C00000-CFFFFF  BIOS (128 KB, espejada)
 *   D00000-DFFFFF  SRAM de respaldo (64 KB, espejada, con bloqueo de escritura)
 *
 * Las lecturas sin mapear devuelven 0xFFFF (en el hardware real es el último dato del bus, casi
 * siempre el prefetch); se cuentan en ng.unmapped_reads.
 */
#include "neogeo.h"

#include "m68k.h"

#define WATCHDOG_CICLOS 1622015          /* 3 244 030 cycles del reloj maestro de 24 MHz */

/* ------------------------------------------------------------------ protección (ROM_MAP.md) */
static uint16_t bit_tabla(uint32_t indice)
{
    uint8_t byte = ng_roms.p[0xDEDD2 + ((indice >> 3) & 0xFFF)];
    return (byte >> (~indice & 7)) & 1;
}

static uint16_t prot_leer(void)
{
    if (ng.prot_cmd == 0x0001) return bit_tabla(ng.prot_cnt++);
    if (ng.prot_cmd == 0x0FFF) {
        uint32_t sel = ((uint32_t)ng.ram[0xF00A] << 8 | ng.ram[0xF00B]) - 1;
        return bit_tabla(sel);
    }
    return 0;
}

static void prot_escribir(uint32_t off, uint16_t v)
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
    int b = v & 7;
    if ((uint32_t)(b + 1) * 0x100000 >= ng_roms.p_size) b = 0;
    ng.p2_bank = b;
}

/* ------------------------------------------------------------------ latch del system */
static void latch(uint32_t a)
{
    uint32_t off = (a & 0x1F) >> 1;
    uint8_t v = (off >> 3) & 1;
    switch (off & 7) {
    case 0: ng.shadow = v; break;
    case 1: ng.cart_vectors = v; break;
    case 5: ng.cart_fix = v; break;             /* también el ROM del Z80 (BIOS/cartucho) */
    case 6: ng.sram_unlocked = v; break;
    case 7: ng.palette_bank = v; break;
    default: break;                                  /* 2-4: tarjeta de memoria */
    }
}

/* ------------------------------------------------------------------ E/S */
static uint16_t leer_es_real(uint32_t a);

static uint16_t leer_es(uint32_t a)
{
    if (ng_trace_reads && ng_cpu.cycles >= ng_trace_from && ng_cpu.cycles <= ng_trace_to &&
        (a & 0x0E0000) != 0x020000 && (a & 0x0E0000) <= 0x080000) {
        uint16_t v = leer_es_real(a);
        fprintf(ng_trace_reads, "%.3f pc=%06X %06X %04X\n", ng_cpu.cycles / 12.0, ng_cpu.pc, a, v);
        return v;
    }
    return leer_es_real(a);
}

static uint16_t leer_es_real(uint32_t a)
{
    if (ng_before_input_read) ng_before_input_read();
    switch (a & 0x0E0000) {
    case 0x000000:                                   /* 300000-31FFFF */
        if (a & 0x80) return 0xFF00 | (ng.raw ? ng.raw_test : ng.test);
        return (uint16_t)((ng.raw ? ng.raw_p1 : (uint8_t)~ng.joy[0]) << 8 | ng.dsw);
    case 0x020000: {                                 /* 320000-33FFFF */
        if (ng.raw)
            return (uint16_t)(ng_sound_reply() << 8 | (ng.raw_coins & 0x3F) | ng_rtc_tp() << 6 | ng_rtc_data() << 7);
        uint8_t monedas = 0x1F;
        if (ng.system[0] & NG_COIN) monedas &= ~0x01;
        if (ng.system[1] & NG_COIN) monedas &= ~0x02;
        if (ng.service) monedas &= ~0x04;
        return (uint16_t)(ng_sound_reply() << 8 | monedas | ng_rtc_tp() << 6 | ng_rtc_data() << 7);
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

/* Escritura en E/S. `mask`: 0xFF00 byte par, 0x00FF byte impar, 0xFFFF palabra. */
/* Traza de escrituras en E/S y palette (mismo formato que tools/mame/trace_io.lua). */
static void trazar(uint32_t a, uint16_t v, uint16_t mask)
{
    if (ng_trace_io && ng_cpu.cycles >= ng_trace_from && ng_cpu.cycles <= ng_trace_to)
        fprintf(ng_trace_io, "%.3f pc=%06X %06X %04X/%04X\n", ng_cpu.cycles / 12.0, ng_cpu.pc, a, v & mask, mask);
}

static void escribir_es(uint32_t a, uint16_t v, uint16_t mask)
{
    trazar(a, v, mask);
    switch (a & 0x0E0000) {
    case 0x000000:
        if (mask & 0x00FF) ng.watchdog = ng_cpu.cycles + WATCHDOG_CICLOS;
        return;
    case 0x020000:
        if (mask & 0xFF00) ng_sound_command((uint8_t)(v >> 8));
        return;
    case 0x080000:                                   /* control de E/S (bytes impares) */
        if (mask & 0x00FF) {
            uint32_t off = (a & 0xFF) >> 1;
            if ((off & 0x78) == 0x28) ng_rtc_write((uint8_t)v);
            /* 0x00 selección de mando, 0x08 bank de tarjeta, 0x10 slot, 0x18/0x20 LEDs,
               0x30/0x70 contadores de monedas: sin efecto en el port */
        }
        return;
    case 0x0A0000:                                   /* latch del system (bytes impares) */
        if (mask & 0x00FF) latch(a);
        return;
    case 0x0C0000:
        /* El LSPC ignora los accesos solo al byte bajo; solo al byte alto copia el byte en
           ambas mitades. */
        if (mask == 0x00FF) return;
        if (mask == 0xFF00) v = (uint16_t)((v & 0xFF00) | v >> 8);
        ng_video_write((a >> 1) & 7, v);
        return;
    default:
        ng.unmapped_writes++;
        return;
    }
}

/* ------------------------------------------------------------------ acceso */
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
            if (f->active && a == f->dir && ng_cpu.pc < 0xC00000)
                v = (v & ~(uint32_t)f->mask) | f->value;
        }
        return v;
    }
    case 0x2: {
        if (a >= 0x2FFFE0 && a <= 0x2FFFEF) return prot_leer();
        uint32_t o = (uint32_t)(ng.p2_bank + 1) * 0x100000 + (a & 0xFFFFF);
        return (uint32_t)ng_roms.p[o] << 8 | ng_roms.p[o + 1];
    }
    case 0x3:
        return leer_es(a);
    case 0x4: case 0x5: case 0x6: case 0x7:
        return ng.palette[(uint32_t)ng.palette_bank << 12 | (a & 0x1FFF) >> 1];
    case 0xC:
        a &= 0x1FFFF;
        return (uint32_t)ng_roms.bios[a] << 8 | ng_roms.bios[a + 1];
    case 0xD:
        a &= 0xFFFF;
        return (uint32_t)ng.sram[a] << 8 | ng.sram[a + 1];
    default:                                         /* 800000-BFFFFF tarjeta (ausente), E-F */
        ng.unmapped_reads++;
        return 0xFFFF;
    }
}

uint32_t bus_r8(uint32_t a)
{
    uint32_t w = bus_r16(a & ~1u);
    return (a & 1) ? (w & 0xFF) : (w >> 8);
}

/* Fin del acceso al bus de una escritura dentro de la instrucción en curso (ng_cpu.op, que
 * empezó en ng_cpu.start_cycle y termina en ng_cpu.cycles). Según el microcódigo del 68000: en MOVE la
 * escritura va antes de la última búsqueda anticipada (termina 4 cycles antes del final); en el
 * resto (lectura-modificación-escritura, CLR...) es el último acceso. Las de 32 bits son dos
 * escrituras seguidas. */
static int64_t fin_escritura(void)
{
    static int64_t start_cycle = -1;
    static int n;
    if (ng_cpu.start_cycle != start_cycle) { start_cycle = ng_cpu.start_cycle; n = 0; }
    n++;
    uint32_t op = ng_cpu.op;
    int move = (op & 0xC000) == 0 && (op & 0x3000) != 0;
    int largo = move ? (op & 0x3000) == 0x2000 : ((op >> 6) & 3) == 2;
    int64_t fin = ng_cpu.cycles - (move ? 4 : 0);
    return (largo && n == 1) ? fin - 4 : fin;
}

/* MAME procesa un evento en T cuando la CPU se detiene en el primer fin de acceso >= T: si un
 * acceso anterior a esta escritura ya terminó en T o después, el evento (p. ej. dibujar una
 * línea) ocurre antes que la escritura. */
static void ordenar_escritura_video(void)
{
    int64_t te = fin_escritura();
    if (ng_next_limit() <= te - 4) ng_catch_up(te - 4);
}

static void escribir(uint32_t a, uint16_t v, uint16_t mask)
{
    a &= 0xFFFFFE;
    if ((a >= 0x3A0000 && a < 0x3E0000) || (a >= 0x400000 && a < 0x800000)) ordenar_escritura_video();
    switch (a >> 20) {
    case 0x1:
        a &= 0xFFFF;
        if (mask & 0xFF00) ng.ram[a] = (uint8_t)(v >> 8);
        if (mask & 0x00FF) ng.ram[a + 1] = (uint8_t)v;
        return;
    case 0x2:
        if (a >= 0x2FFFE0 && a <= 0x2FFFEF) { prot_escribir(a - 0x2FFFE0, v); return; }
        if (a >= 0x2FFFF0) { p2_bank(v); return; }
        ng.unmapped_writes++;
        return;
    case 0x3:
        escribir_es(a, v, mask);
        return;
    case 0x4: case 0x5: case 0x6: case 0x7: {
        trazar(a, v, mask);
        uint32_t i = (uint32_t)ng.palette_bank << 12 | (a & 0x1FFF) >> 1;
        uint16_t nuevo = (uint16_t)((ng.palette[i] & ~mask) | (v & mask));
        ng_palette_write(i, nuevo);
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

void bus_w16(uint32_t a, uint32_t v) { escribir(a, (uint16_t)v, 0xFFFF); }

void bus_w8(uint32_t a, uint32_t v)
{
    /* El 68000 pone el byte en ambas mitades del bus de data; UDS/LDS eligen la mitad. */
    v &= 0xFF;
    escribir(a, (uint16_t)(v << 8 | v), (a & 1) ? 0x00FF : 0xFF00);
}

/* ------------------------------------------------------------------ Musashi (respaldo) */
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
    ng.p2_bank = 0;
    ng.prot_cmd = ng.prot_cnt = 0;
    ng.shadow = ng.cart_vectors = ng.cart_fix = ng.sram_unlocked = ng.palette_bank = 0;
    ng.dsw = 0xFF;
    ng.test = 0x80;                  /* bit 7: botón de test sin pulsar; bit 6: MVS */
    ng.watchdog = ng_cpu.cycles + WATCHDOG_CICLOS;
}
