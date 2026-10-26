/* Sound subsystem: Z80 (floooh's z80.h, zlib) + YM2610 (ymfm, BSD-3).
 *
 * Z80 map (4 MHz), per the MAME 0.289 documentation (neogeo.cpp):
 *   0000-7FFF  fixed M1 (no SM1: boots from the M1, like the AES; MAME uses a copy of the M1
 *              as SM1, see docs/ROM_MAP.md)
 *   8000-BFFF, C000-DFFF, E000-EFFF, F000-F7FF  16/8/4/2 KB M1 windows; the bank is
 *              selected by READING port 0x08-0x0B (mirror 0xF0) with the bank in the high byte:
 *              address = bank << (11 + window), window 0 = F000 ... 3 = 8000
 *   F800-FFFF  RAM (2 KB)
 * Ports (low byte): 00 reads the 68000 command (and acknowledges it) / writing clears it;
 *   04-07 YM2610; 08 / 18 enables / disables the NMI; 0C writes the reply to the 68000.
 * The NMI is active while there is an unread command and the NMI is enabled; the YM2610 IRQ
 * goes to the INT line (the program uses IM 1).
 *
 * Time: everything is measured in 24 MHz master clock cycles (68000 = 2, Z80 = 6, YM = 3; the
 * samples, at 55 555 Hz as in MAME, see SAMPLE_HZ).
 *
 * Interleaving with the 68000, like MAME's scheduler (the oracle):
 *   - the Z80 advances up to each 68000 event (line start, VBlank, YM timers...), which in
 *     MAME are the boundaries of the execution slices;
 *   - a 68000 command synchronises the Z80 at that instant and opens a 50 us window of
 *     fine-grained interleaving (MAME: perfect_quantum in audio_command_w);
 *   - outside that window, the 68000 only sees the Z80 reply in the next slice
 *     (measured: 15.8-33.9 us of delay, tools/mame/z80_latency.lua).
 */
#include "neogeo.h"
#include "ym2610.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "z80.h"

#define MASTER_PER_68K 2
#define MASTER_PER_Z80 6
#define MASTER_PER_YM 3
/* Samples: MAME generates the YM2610 stream at exactly 55 555 Hz (the frequency is an integer),
   not at 8 MHz / 144 = 55 555.55 Hz. Since each sample advances the YM state (envelopes,
   ADPCM), the same cadence is used: sample k ends at ceil((k + 1) * 24e6 / 55 555) +
   SAMPLE_PHASE. The phase (one Z80 T-state) places the Z80's YM writes on the same side of the
   sample boundaries as in MAME: measured on Metal Slug 3's attract mode, whose audio is then
   identical to MAME's -wavwrite sample for sample (a key-on falls within 0.25 us of a boundary). */
#define SAMPLE_HZ 55555
#define MASTER_HZ 24000000
#define SAMPLE_PHASE 6
#define Z80_RETN_LAST_STEP 1218          /* z80.h decoder steps (see tick_z80) */
#define Z80_RETI_LAST_STEP 1252
#define FINE_WINDOW 600                  /* 68000 cycles = 50 us */
/* MAME's YM2610 SSG is a separate sound device (ymfm's ssg_override) whose stream is brought up
   to date lazily: when its registers are written and when the sound system updates every stream,
   every 20 ms from power-on. The soft reset does not update it first, so in effect the SSG is
   reset at its last update: the time since then is run again by the reset SSG. Here the SSG is
   clocked inside each output sample (ymfm's 2:9 resampler: 5 SSG clocks in even samples, 4 in
   odd ones), so after the reset it runs the clocks of the samples since that update, less those
   of the extra sample the reset adds (MAME's separate SSG does not run it). Measured: the SSG
   noise generator's phase after Metal Slug 3's first-boot watchdog reset (994 clocks: 999 - 5)
   and the audio of the user's recording; tools/ymprobe, docs/PHASE3_RECOMP.md. */
#define SSG_UPDATE_PERIOD (MASTER_HZ / 50)
#define AUDIO_CAP 65536                  /* stereo samples in the ring buffer */

static struct {
    z80_t z80;
    uint64_t pins;
    int64_t t;                           /* master cycle reached by the Z80 */
    int64_t now;                         /* master cycle of the current event (YM callbacks) */
    uint8_t ram[0x800];
    uint8_t bank[4];
    uint8_t command, pending, nmi_enabled, reply;
    int irq_ym;
    int int_ack;                         /* inside an INT acknowledge sequence: NMI not sampled */
    int64_t timer[2];                    /* expiry (master), -1 = stopped */
    int64_t busy_end;
    int64_t next_sample;
    int64_t n_samples;
    int64_t fine_until;                  /* 68000 cycle */
    int16_t audio[AUDIO_CAP][2];
    uint32_t write_pos, read_pos;
    int64_t ym_samples;                  /* samples the chip has made (ymfm resampler index) */
    int64_t ssg_update;                  /* last 20 ms update period of MAME's SSG stream */
    int64_t ssg_clocks;                  /* SSG clocks since the last update of that stream */
} s;

int ng_audio_enabled;
void (*ng_on_sound_command)(uint8_t command);
NgNotes ng_notes;
static uint8_t ym_addr[2];               /* last register selected in each bank (ng_notes) */

/* ------------------------------------------------------------------ YM2610 */
void ng_ym_timer(uint32_t n, int32_t cycles)
{
    s.timer[n & 1] = cycles < 0 ? -1 : s.now + (int64_t)cycles * MASTER_PER_YM;
}

void ng_ym_busy_until(uint32_t cycles) { s.busy_end = s.now + (int64_t)cycles * MASTER_PER_YM; }
int ng_ym_busy(void) { return s.now < s.busy_end; }
void ng_ym_irq(int active) { s.irq_ym = active; }

uint8_t ng_ym_rom(uint32_t d)
{
    d &= 0xFFFFFF;
    return d < ng_roms.adpcm_size ? ng_roms.adpcm[d] : 0;
}

static int16_t saturate(double v)
{
    return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v);
}

static int ssg_clocks_in(int64_t sample_index) { return sample_index & 1 ? 4 : 5; }

static void sample(void)
{
    int32_t o[3];
    s.ssg_clocks += ssg_clocks_in(s.ym_samples++);
    ym_generate(o);
    if (!ng_audio_enabled) return;
    /* MAME mix (neogeo_stereo): SSG x0.84 to both channels; FM left/right x0.98. */
    uint32_t i = s.write_pos % AUDIO_CAP;
    /* in single precision on samples normalised to +-1, as MAME's sound streams (a double
       product differs by one unit in about 0.01 % of the samples) */
    float fm_l = (float)o[0] / 32768.0f, fm_r = (float)o[1] / 32768.0f, ssg = (float)o[2] / 32768.0f;
    s.audio[i][0] = saturate((fm_l * 0.98f + ssg * 0.84f) * 32768.0f);
    s.audio[i][1] = saturate((fm_r * 0.98f + ssg * 0.84f) * 32768.0f);
    s.write_pos++;
    if (s.write_pos - s.read_pos > AUDIO_CAP) s.read_pos = s.write_pos - AUDIO_CAP;   /* drop the old data */
}

uint32_t ng_audio_read(int16_t *dest, uint32_t max)
{
    uint32_t n = 0;
    while (n < max && s.read_pos != s.write_pos) {
        uint32_t i = s.read_pos++ % AUDIO_CAP;
        dest[2 * n] = s.audio[i][0];
        dest[2 * n + 1] = s.audio[i][1];
        n++;
    }
    return n;
}

/* ------------------------------------------------------------------ Z80 bus */
static FILE *z80_trace, *ym_trace;
static int64_t z80_trace_from, z80_trace_to;
static int z80_trace_ready;

static uint8_t mem_read(uint16_t a)
{
    const uint8_t *m1 = ng_roms.z80;
    const uint32_t m1_mask = ng_roms.z80_size - 1;        /* M1 sizes are powers of two */
    if (a < 0x8000) return m1[a];
    if (a < 0xC000) return m1[(((uint32_t)s.bank[3] << 14) + (a & 0x3FFF)) & m1_mask];
    if (a < 0xE000) return m1[(((uint32_t)s.bank[2] << 13) + (a & 0x1FFF)) & m1_mask];
    if (a < 0xF000) return m1[(((uint32_t)s.bank[1] << 12) + (a & 0x0FFF)) & m1_mask];
    if (a < 0xF800) return m1[(((uint32_t)s.bank[0] << 11) + (a & 0x07FF)) & m1_mask];
    return s.ram[a & 0x7FF];
}

static uint8_t port_read(uint16_t p)
{
    uint8_t low = (uint8_t)p;
    if (low == 0x00) { s.pending = 0; return s.command; }
    if (low >= 0x04 && low <= 0x07) return ym_read(low & 3);
    if ((low & 0x0F) >= 0x08 && (low & 0x0F) <= 0x0B) { s.bank[low & 3] = (uint8_t)(p >> 8); return 0; }
    return 0;
}

static void port_write(uint16_t p, uint8_t v)
{
    uint8_t low = (uint8_t)p;
    if (ym_trace && s.t >= z80_trace_from && s.t <= z80_trace_to)
        fprintf(ym_trace, "%.3f %02X %02X\n", s.t / 24.0, low, v);
    if (low == 0x00) s.command = 0;
    else if (low >= 0x04 && low <= 0x07) {
        int bank = (low >> 1) & 1;
        if (!(low & 1)) ym_addr[bank] = v;
        else if (bank == 0) {
            if (ym_addr[0] == 0x28 && (v & 0xF0)) ng_notes.fm++;               /* FM key on */
            else if (ym_addr[0] >= 0x08 && ym_addr[0] <= 0x0A && (v & 0x1F)) ng_notes.ssg++;
            else if (ym_addr[0] == 0x10 && (v & 0x80)) ng_notes.adpcm_b++;     /* ADPCM-B start */
            if (ym_addr[0] < 0x10) s.ssg_clocks = 0;           /* SSG register: MAME updates the SSG */
        } else if (ym_addr[1] == 0x00 && !(v & 0x80) && (v & 0x3F)) {
            ng_notes.adpcm_a++;                                                /* ADPCM-A key on */
        }
        ym_write(low & 3, v);
    }
    else if (low == 0x08) s.nmi_enabled = 1;
    else if (low == 0x18) s.nmi_enabled = 0;
    else if (low == 0x0C) {
        s.reply = v;
        if (ng_trace) fprintf(ng_trace, "%.3f RESP %02X\n", s.t / 24.0, v);
    }
}

/* Per-instruction Z80 trace (diagnostics): PD_TRACE_Z80=file,from_us,to_us. Format
   "C=<Z80 cycle> <PC>:", comparable with the MAME debugger trace (totalcycles). */
static void z80_trace_start(void)
{
    z80_trace_ready = 1;
    const char *e = getenv("PD_TRACE_Z80");
    double d, h;
    char path[1024];
    if (!e || sscanf(e, "%1023[^,],%lf,%lf", path, &d, &h) != 3) return;
    if (getenv("PD_TRACE_YM")) ym_trace = fopen(path, "w");   /* port writes only */
    else z80_trace = fopen(path, "w");
    z80_trace_from = (int64_t)(d * 24.0);
    z80_trace_to = (int64_t)(h * 24.0);
}

static void tick_z80(void)
{
    if (!z80_trace_ready) z80_trace_start();
    if (z80_trace && z80_opdone(&s.z80) && s.t >= z80_trace_from && s.t <= z80_trace_to)
        fprintf(z80_trace, "C=%lld %04X:\n", (long long)(s.t / MASTER_PER_Z80), s.z80.pc);
    /* MAME's Z80 runs at least one instruction of an interrupt handler before it takes another
       interrupt (measured, Metal Slug 5: an NMI that arrives during the YM2610 IRQ's acknowledge
       is taken after the DI at 0x0038). z80.h would take it at the end of the acknowledge
       sequence, so the NMI is hidden until that sequence ends, and an NMI edge it latched since
       the sequence began is forgotten (it is latched again then, and z80.h takes it at the end
       of the handler's first instruction). An NMI latched before the sequence began would have
       been taken instead of the IRQ. */
    if (s.int_ack && z80_opdone(&s.z80)) s.int_ack = 0;
    uint64_t p = s.pins & ~(Z80_NMI | Z80_INT);
    if (s.pending && s.nmi_enabled && !s.int_ack) p |= Z80_NMI;
    if (s.irq_ym) p |= Z80_INT;
    /* z80.h fetches the next instruction (where it accepts interrupts) before RETN/RETI copy IFF2
       into IFF1, so an interrupt pending when the NMI handler returns waits one more
       instruction; MAME's Z80 takes it right away (tools/mame/cycles.lua, Metal Slug 3). The copy
       is done before the tick of that last step (z80.h steps, commit 9e88298). */
    if (s.z80.step == Z80_RETN_LAST_STEP || s.z80.step == Z80_RETI_LAST_STEP) s.z80.iff1 = s.z80.iff2;
    p = z80_tick(&s.z80, p);
    if (p & Z80_MREQ) {
        uint16_t a = Z80_GET_ADDR(p);
        if (p & Z80_RD) { Z80_SET_DATA(p, mem_read(a)); }
        else if ((p & Z80_WR) && a >= 0xF800) s.ram[a & 0x7FF] = Z80_GET_DATA(p);
    } else if (p & Z80_IORQ) {
        uint16_t a = Z80_GET_ADDR(p);
        if (p & Z80_M1) {                                   /* INT acknowledge (IM 1) */
            Z80_SET_DATA(p, 0xFF);
            s.int_ack = 1;
            s.z80.int_bits &= ~Z80_NMI;
            p &= ~Z80_NMI;
        }
        else if (p & Z80_RD) { Z80_SET_DATA(p, port_read(a)); }
        else if (p & Z80_WR) port_write(a, Z80_GET_DATA(p));
    }
    s.pins = p;
}

/* YM timers and samples up to master cycle t. */
static void peripherals_until(int64_t t)
{
    for (;;) {
        int64_t next = s.next_sample;
        int n = -1;
        for (int i = 0; i < 2; i++)
            if (s.timer[i] >= 0 && s.timer[i] < next) { next = s.timer[i]; n = i; }
        if (next > t) break;
        s.now = next;
        if (n >= 0) {
            s.timer[n] = -1;
            ym_timer_expired((uint32_t)n);
        } else {
            if (next / SSG_UPDATE_PERIOD != s.ssg_update) {   /* MAME's 20 ms stream update */
                s.ssg_update = next / SSG_UPDATE_PERIOD;
                s.ssg_clocks = 0;
            }
            sample();
            s.n_samples++;
            s.next_sample = ((s.n_samples + 1) * MASTER_HZ + SAMPLE_HZ - 1) / SAMPLE_HZ + SAMPLE_PHASE;
        }
    }
}

/* Interrupt timing. MAME's Z80 takes an interrupt raised at T at the end of the instruction
 * running at T. z80.h only starts it at the next fetch if the pin was already set during the
 * T-state before the instruction's last one, and here a pin changed by an event at T is seen from
 * the next T-state. So the interrupt sources are applied IRQ_LEAD master cycles early: a YM2610
 * timer that expires at T before the T-state that starts at T - IRQ_LEAD (the samples keep their
 * own order), and a 68000 command (NMI) likewise. Measured against MAME's trace of the Z80 port
 * writes (tools/mame/trace_ym.lua, Metal Slug 3 attract mode): with it every write keeps the same
 * offset; without it, the writes after an interrupt arrive one instruction late now and then. */
#define IRQ_LEAD (2 * MASTER_PER_Z80)

/* Z80, YM timers and samples up to master cycle `end`. */
static void run_until(int64_t end)
{
    while (s.t + MASTER_PER_Z80 <= end) {
        for (;;) {
            int n = -1;
            for (int i = 0; i < 2; i++)
                if (s.timer[i] >= 0 && s.timer[i] <= s.t + IRQ_LEAD && (n < 0 || s.timer[i] < s.timer[n])) n = i;
            if (n < 0) break;
            s.now = s.timer[n];
            s.timer[n] = -1;
            ym_timer_expired((uint32_t)n);
        }
        s.now = s.t;
        tick_z80();
        s.t += MASTER_PER_Z80;
        peripherals_until(s.t);
    }
}

void ng_sound_until(int64_t cycles_68k)
{
    run_until(cycles_68k * MASTER_PER_68K);
}

int64_t ng_sound_next_event(void)
{
    int64_t t = INT64_MAX;
    for (int i = 0; i < 2; i++)
        if (s.timer[i] >= 0 && s.timer[i] < t) t = s.timer[i];
    return t == INT64_MAX ? t : (t + MASTER_PER_68K - 1) / MASTER_PER_68K;
}

/* ------------------------------------------------------------------ 68000 side */
void ng_sound_command(uint8_t v, int64_t when)
{
    if (ng_trace)
        fprintf(ng_trace, "%.3f f=%llu line=%lld pc=%06X CMD %02X\n", ng_cpu.cycles / 12.0,
                (unsigned long long)ng.frames, (long long)((ng_cpu.cycles - ng.frame_start) / NG_CYCLES_PER_LINE), ng_cpu.pc, v);
    run_until(when * MASTER_PER_68K - IRQ_LEAD + MASTER_PER_Z80 - 1);   /* NMI: see IRQ_LEAD */
    s.command = v;
    s.pending = 1;
    s.fine_until = ng_cpu.cycles + FINE_WINDOW;
    if (ng_on_sound_command) ng_on_sound_command(v);
}

void ng_sound_only_command(uint8_t command)
{
    ng_cpu.cycles = s.t / MASTER_PER_68K;
    ng_sound_command(command, ng_cpu.cycles);
}

void ng_sound_only_advance(int64_t cycles_68k)
{
    ng_sound_until(s.t / MASTER_PER_68K + cycles_68k);
}

uint8_t ng_sound_reply(void)
{
    if (ng_cpu.cycles <= s.fine_until) {
        ng_sound_until(ng_cpu.cycles);
    } else {
        /* The bus read happens near the end of the instruction (before the last prefetch):
           ng_cpu.cycles already includes the whole instruction. If a MAME slice boundary falls
           before that read, the Z80 has already run up to it (measured in Phase 4: MAME sees
           the reply on the read that starts 5 cycles before a line start). */
        int64_t read_at = ng_cpu.cycles - 8, limit = ng_next_limit();
        if (limit <= read_at) ng_sound_until(limit);
    }
    static int seen = -1;
    if (ng_trace && s.reply != seen) {
        fprintf(ng_trace, "%.3f VISTO %02X\n", ng_cpu.cycles / 12.0, s.reply);
        seen = s.reply;
    }
    return s.reply;
}

void ng_sound_state(NgState *e)
{
    ng_state_block(e, &s, sizeof s);
    uint32_t n = ym_state_size();
    uint32_t n_saved = n;
    ng_state_block(e, &n_saved, sizeof n_saved);
    if (e->saving) {
        if (e->data) ym_state_save(e->data + e->pos);
    } else {
        s.read_pos = s.write_pos;                /* pending audio is not replayed */
        if (n_saved == n && e->pos + n <= e->size) ym_state_load(e->data + e->pos, n);
    }
    e->pos += n;
}

void ng_sound_init(void)
{
    int active = ng_audio_enabled;
    memset(&s, 0, sizeof s);
    ng_audio_enabled = active;
    s.pins = z80_init(&s.z80);
    /* MAME starts the Z80 with its registers at 0 (z80.h: 0xFFFF, "The Undocumented Z80
       Documented"); Metal Slug 3's driver writes A to port 0x18 before loading it. */
    s.z80.af = s.z80.bc = s.z80.de = s.z80.hl = s.z80.ix = s.z80.iy = s.z80.sp = s.z80.wz = 0;
    s.z80.af2 = s.z80.bc2 = s.z80.de2 = s.z80.hl2 = 0;
    /* MAME's initial banks (identity); the real NEO-ZMC starts at 0, but the sound program
       sets them before using them. */
    s.bank[0] = 0x1E; s.bank[1] = 0x0E; s.bank[2] = 0x06; s.bank[3] = 0x02;
    s.timer[0] = s.timer[1] = -1;
    s.next_sample = (MASTER_HZ + SAMPLE_HZ - 1) / SAMPLE_HZ + SAMPLE_PHASE;
    ym_reset();
}

/* As MAME's soft reset (checked with tools/mame/trace_ym.lua after Metal Slug 3's first-boot
   watchdog reset): the Z80 restarts at 0 with interrupts off (IM 0, I and R cleared) but keeps
   its other registers and its RAM; the YM2610 is reset; the command latch is acknowledged and the
   NMI disabled (MAME's machine_reset); the NEO-ZMC banks, the reply, the clocks and the audio
   stream go on. In MAME's audio the reset chip also produces one sample more than the sample
   clock accounts for (its envelope and ADPCM-A phase, and the length of the stream, only match
   the -wavwrite output that way). */
void ng_sound_reset(void)
{
    z80_t z = s.z80;
    s.pins = z80_reset(&s.z80);
    s.z80.af = z.af; s.z80.bc = z.bc; s.z80.de = z.de; s.z80.hl = z.hl;
    s.z80.ix = z.ix; s.z80.iy = z.iy; s.z80.sp = z.sp;
    s.z80.af2 = z.af2; s.z80.bc2 = z.bc2; s.z80.de2 = z.de2; s.z80.hl2 = z.hl2;
    s.pending = 0;
    s.nmi_enabled = 0;
    s.irq_ym = 0;
    s.timer[0] = s.timer[1] = -1;
    s.busy_end = 0;
    ym_reset();
    if (s.t / SSG_UPDATE_PERIOD != s.ssg_update) s.ssg_clocks = 0;   /* an update since the last sample */
    int64_t redo = s.ssg_clocks - ssg_clocks_in(s.ym_samples);       /* see SSG_UPDATE_PERIOD */
    if (redo > 0) ym_ssg_clock((uint32_t)redo);
    sample();                            /* MAME's stream emits one more sample (see above) */
}
