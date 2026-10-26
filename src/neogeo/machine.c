/* Machine scheduler: LSPC lines, interrupts, watchdog and the 68000 loop.
 *
 * The recompiled 68000 runs until rc_cycle_limit; there the dispatcher calls rc_event, which
 * processes every event that is due and delivers the highest-level pending interrupt if the
 * mask allows it. Events (68000 cycles from vpos 0 of the frame):
 *   - start of each line (768 cycles): draws the previous one, builds the sprite list;
 *     on line 256 the auto-animation advances;
 *   - VBlank (IRQ1): line 240 + 29 cycles (58 master clock cycles after HSYNC);
 *   - IRQ2 counter reload at VBlank (if enabled): line 240 + 573 cycles;
 *   - IRQ2 (position counter), in pixel clock steps (2 cycles);
 *   - watchdog: ~8 frames without writing to 300001 reset the machine.
 * Levels: VBlank = 1, IRQ2 = 2, IRQ3 = 3 (pending since power-on; the BIOS acknowledges it).
 *
 * Interrupt entry, measured against MAME's cycle-exact core (Phase 4,
 * tools/mame/cycles.lua + compare_cycles.py, 29 interrupts):
 *   - sampling: an IRQ raised at T is taken at the end of an instruction if T <= L, where L is
 *     the last point where MAME can stop the CPU (end of a bus access or start of the
 *     instruction) before the step at which the instruction samples the interrupts
 *     (src/cpu/cycles68k.h, verified opcode by opcode and with traces);
 *   - duration: the autovectored acknowledge (VPA) happens 10 cycles after the end of the
 *     instruction and is synchronised with the E clock (period 10, MAME m68000.cpp vpa_sync);
 *     then 35 more cycles follow. Total = 10 + E wait + 35 (48 to 58 cycles, not the nominal 44).
 *
 * Watchdog reset (Metal Slug 3's first boot ends with one): MAME's timing afterwards shifts
 * against the 68000 in a way the hardware would not.
 *   - The 68000 restarts from the expiry, not from the end of the instruction that crossed
 *     it, and its cycle count (the E clock phase) starts over 2 cycles before the expiry: the
 *     first instruction starts 32 cycles after it and is counted as cycle 34.
 *   - The line starts (drawing and sprite lists; also the beam position MAME reports) come
 *     1 cycle earlier relative to the 68000, while the VBlank IRQ and the vertical counter
 *     the game reads do not move (ng.video_offset).
 * Measured, not derived: cycle traces (tools/mame/cycles.lua), the beam position in MAME's
 * debugger trace (beamx/beamy) and the attract mode's 10 616 VBlanks and 10 002 images,
 * identical to MAME only with both.
 *
 * Frame phase: at power-on the beam is at the start of VBlank (vpos 240), as in MAME; the I/O
 * trace confirms it (tools/mame/trace_io.lua): with the origin at vpos 0 all the writes
 * synchronised with VBlank arrived 24 lines (1536 us) earlier than in MAME. That is why the
 * first VBlank arrives a full frame after power-on, and each frame "ends" (control returns to
 * the host) at the start of VBlank, with the complete image: frame N of the port is frame N of
 * MAME (screen:frame_number()).
 */
#include "neogeo.h"

#include <stdio.h>
#include <string.h>

#include "cycles68k.h"
#include "dispatcher.h"

NgMachine ng;
FILE *ng_trace;
FILE *ng_trace_io;
int64_t ng_trace_from, ng_trace_to;
FILE *ng_trace_reads;
void (*ng_on_vblank_ack)(void);
void (*ng_before_input_read)(void);
NgRoms ng_roms;
uint32_t ng_pc_vblank;
NgDebug ng_dbg;
NgForcedRead ng_forced_read[NG_FORCED_COUNT];
Cpu68k ng_cpu;

#define T_VBLANK (NG_VBLANK_LINE * NG_CYCLES_PER_LINE + 29)
#define T_RELOAD (NG_VBLANK_LINE * NG_CYCLES_PER_LINE + 573)
#define MIN_SAMPLING 0          /* L <= end: no IRQ gets in before T */

static int64_t irq_since[4];               /* cycle at which each level was raised */

static int vblank_done, reload_done;
int ng_reset_cycles = 34;              /* MAME: the first instruction starts at cycle 34 */

/* Cycles of the entry to an interrupt that starts at the end of the instruction at s. The E
   clock phase counts from the last 68000 reset (MAME restarts the cycle count). */
static int irq_entry_cycles(int64_t s)
{
    int64_t t = s + 10 - ng.eclock_origin;      /* acknowledge cycle (IACK) */
    int m = (int)(t % 10);
    int wait = m < 7 ? 10 - m : 20 - m;         /* up to the next usable E clock edge */
    return 10 + wait + 35;
}

void ng_irq_update(void)
{
    rc_irq_level = ng.irq3 ? 3 : ng.irq_raster ? 2 : ng.irq_vblank ? 1 : 0;
}

static void reset_cpu(void)
{
    Cpu68k *c = &ng_cpu;
    int64_t cycles = c->cycles;
    ng.eclock_origin = cycles;
    memset(c, 0, sizeof *c);
    c->cycles = cycles;
    c->s = 1;
    c->imask = 7;
    c->op = 0x4E75;                        /* default class: last access at the end */
    c->start_cycle = c->cycles;
    c->a[7] = bus_r32(0);
    c->pc = bus_r32(4) & 0xFFFFFF;
    c->cycles += ng_reset_cycles;           /* 68000 reset sequence */
}

static void watchdog_reset(void)
{
    ng.watchdog_resets++;
    fprintf(stderr, "watchdog: machine reset (frame %llu, PC %06X)\n",
            (unsigned long long)ng.frames, ng_cpu.pc);
    ng_cpu.cycles = ng.watchdog - 2;     /* MAME's restart point (see the top of the file) */
    ng_bus_init();
    ng_sound_reset();            /* the watchdog reset affects the whole machine */
    ng.irq2_control = 0;
    ng.irq2_when = -1;
    ng.video_offset = -1;
    reset_cpu();
}

void ng_power_on(void)
{
    memset(&ng, 0, sizeof ng);
    memset(&ng_cpu, 0, sizeof ng_cpu);
    ng_video_init();
    ng_bus_init();
    ng_rtc_init();
    ng_sound_init();
    ng.irq3 = 1;
    ng_irq_update();
    ng.frame_start = -(int64_t)NG_VBLANK_LINE * NG_CYCLES_PER_LINE;   /* t = 0 at vpos 240 */
    ng.line = NG_VBLANK_LINE + 1;
    vblank_done = reload_done = 1;
    irq_since[1] = irq_since[2] = irq_since[3] = 0;
    reset_cpu();
}

/* Next scheduled event (absolute cycle). */
static int64_t next_event(void)
{
    int64_t t = ng.frame_start + ng.video_offset + (int64_t)ng.line * NG_CYCLES_PER_LINE;
    if (!vblank_done && ng.frame_start + T_VBLANK < t) t = ng.frame_start + T_VBLANK;
    if (!reload_done && ng.frame_start + T_RELOAD < t) t = ng.frame_start + T_RELOAD;
    if (ng.irq2_when >= 0 && ng.irq2_when < t) t = ng.irq2_when;
    if (ng.watchdog < t) t = ng.watchdog;
    int64_t ts = ng_sound_next_event();        /* YM timers also cut the slice */
    if (ts < t) t = ts;
    return t;
}

int64_t ng_next_limit(void) { return next_event(); }

/* Processes one due event. Returns 1 if the frame ended. */
static int process_event(int64_t now)
{
    if (ng.watchdog <= now) {
        watchdog_reset();
        return 0;
    }
    if (ng.irq2_when >= 0 && ng.irq2_when <= now) {
        int64_t t = ng.irq2_when;
        ng.irq2_when = -1;
        if (ng.irq2_control & 0x10) {
            if (!ng.irq_raster) irq_since[2] = t;
            ng.irq_raster = 1;
            ng_irq_update();
        }
        if (ng.irq2_control & 0x80)                                    /* repeat */
            ng.irq2_when = t + ((int64_t)ng.irq2_counter + 1) * 2;
        return 0;
    }
    if (!vblank_done && ng.frame_start + T_VBLANK <= now) {
        vblank_done = 1;
        if (!ng.irq_vblank) irq_since[1] = ng.frame_start + T_VBLANK;
        ng.irq_vblank = 1;
        ng_irq_update();
        return 0;
    }
    if (!reload_done && ng.frame_start + T_RELOAD <= now) {
        reload_done = 1;
        if (ng.irq2_control & 0x40)
            ng.irq2_when = ng.frame_start + T_RELOAD + ((int64_t)ng.irq2_counter + 1) * 2;
        return 0;
    }
    /* line start (if it is not yet time for the line, the event was a YM2610 timer, already
       handled by ng_sound_until) */
    if (ng.frame_start + ng.video_offset + (int64_t)ng.line * NG_CYCLES_PER_LINE > now) return 0;
    if (ng.line == NG_LINES) {
        ng.frame_start += NG_CYCLES_PER_FRAME;
        ng.line = 0;
        vblank_done = reload_done = 0;
    }
    if (ng_dbg.active && ng_cpu.pc >= ng_dbg.wait_start && ng_cpu.pc < ng_dbg.wait_end)
        ng_dbg.wait_lines_current++;
    ng_video_line_start(ng.line);
    if (ng.line == 0x100) ng_video_anim_tick();
    ng.line++;
    if (ng.line - 1 == NG_VBLANK_LINE) {        /* complete image: end of frame */
        ng.frames++;
        if (ng_dbg.active) {
            ng_dbg.wait_lines = ng_dbg.wait_lines_current;
            ng_dbg.wait_lines_current = 0;
            memcpy(ng_dbg.box, ng_dbg.box_current, sizeof ng_dbg.box);
            for (int i = 0; i < 381; i++) ng_dbg.box_current[i] = (NgBox){1, 0, 1, 0};
        }
        return 1;
    }
    return 0;
}

void ng_catch_up(int64_t t)
{
    for (;;) {
        int64_t e = next_event();
        if (e > t) break;
        ng_sound_until(e);
        if (process_event(e)) rc_exit = 1;
    }
}

static void deliver_irq(Cpu68k *c, int level)
{
    if (level == 1) ng_pc_vblank = c->pc;
    uint32_t sr = cpu_get_sr(c);
    if (!c->s) { c->usp = c->a[7]; c->a[7] = c->ssp; c->s = 1; }
    c->t = 0;
    c->imask = (uint8_t)level;
    c->a[7] -= 4; bus_w32(c->a[7], c->pc);
    c->a[7] -= 2; bus_w16(c->a[7], sr);
    c->pc = bus_r32((uint32_t)(24 + level) * 4) & 0xFFFFFF;
    c->op = 0x4E75; c->start_cycle = c->cycles;     /* the entry ends with bus accesses */
    c->cycles += CIC(irq_entry_cycles(c->cycles));
    c->stopped = 0;
}

void rc_event(Cpu68k *c)
{
    for (;;) {
        int64_t t = next_event();
        if (c->cycles >= t) {
            /* Like MAME's scheduler: the Z80 runs up to the slice limit. */
            ng_sound_until(t);
            if (process_event(t)) { rc_exit = 1; break; }
            continue;
        }
        int visible = 0;
        for (int n = 3; n >= 1 && !visible; n--) {
            int pend = n == 3 ? ng.irq3 : n == 2 ? ng.irq_raster : ng.irq_vblank;
            if (pend && irq_since[n] <= cycles_access_l(c)) visible = n;
        }
        if (visible > c->imask) {
            deliver_irq(c, visible);
            continue;                       /* the entry consumes cycles: another event may fall due */
        }
        if (c->stopped) {                  /* STOP: wait for the next event or the IRQ */
            int64_t d = t;
            for (int n = 1; n <= 3; n++) {
                int pend = n == 3 ? ng.irq3 : n == 2 ? ng.irq_raster : ng.irq_vblank;
                if (pend && n > c->imask && irq_since[n] + MIN_SAMPLING < d) d = irq_since[n] + MIN_SAMPLING;
            }
            c->cycles = d > c->cycles ? d : c->cycles + 1;
            continue;
        }
        break;
    }
    /* Execution limit: the next event or the first moment at which a pending, unmasked IRQ
       could be taken (MIN_SAMPLING cycles after it is raised). */
    int64_t limit = next_event();
    for (int n = 1; n <= 3; n++) {
        int pend = n == 3 ? ng.irq3 : n == 2 ? ng.irq_raster : ng.irq_vblank;
        if (pend && n > c->imask && irq_since[n] + MIN_SAMPLING < limit) limit = irq_since[n] + MIN_SAMPLING;
    }
    rc_cycle_limit = limit;
}

void ng_machine_state(NgState *e)
{
    ng_state_block(e, &ng, sizeof ng);
    ng_state_block(e, &ng_cpu, sizeof ng_cpu);
    ng_state_block(e, irq_since, sizeof irq_since);
    ng_state_block(e, &vblank_done, sizeof vblank_done);
    ng_state_block(e, &reload_done, sizeof reload_done);
    if (!e->saving) ng_irq_update();
}

/* Nested call into the game's code between frames (cheats.c): runs `target` with the given
   registers until it returns, with interrupts masked and no hardware events, then restores the
   68000 exactly (registers, PC, cycles); only memory keeps the effects. Returns 1 if the routine
   returned within max_steps dispatch steps. */
int ng_call68k(uint32_t target, const uint32_t d[8], const uint32_t a[8], long max_steps)
{
    enum { RET = 0x0FFFFEu };                  /* never executed: the call ends when PC gets here */
    Cpu68k saved = ng_cpu;
    int64_t limit = rc_cycle_limit;
    int exit_req = rc_exit;
    rc_cycle_limit = INT64_MAX;
    rc_exit = 0;
    for (int i = 0; i < 8; i++) { ng_cpu.d[i] = d[i]; ng_cpu.a[i] = a[i]; }
    ng_cpu.imask = 7;
    ng_cpu.stopped = 0;
    ng_cpu.a[7] -= 4;
    bus_w32(ng_cpu.a[7], RET);
    const uint32_t ret_sp = ng_cpu.a[7] + 4;
    ng_cpu.pc = target & 0xFFFFFF;
    int ok = 0;
    for (long n = 0; n < max_steps; n++) {
        if (ng_cpu.pc == RET && ng_cpu.a[7] == ret_sp) { ok = 1; break; }
        if (ng_cpu.a[7] > ret_sp) break;
        rc_step(&ng_cpu);
    }
    ng_cpu = saved;
    rc_cycle_limit = limit;
    rc_exit = exit_req;
    return ok;
}

void ng_run_frame(void)
{
    rc_exit = 0;
    rc_cycle_limit = 0;                   /* immediately processes whatever is due */
    rc_run(&ng_cpu);
}
