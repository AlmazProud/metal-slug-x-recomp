/* NEC uPD4990A calendar clock in serial mode (C0-C2 = 1, CS = OE = 1 hard-wired on the MVS).
 *
 * The 68000 drives it with three lines (DATA IN, CLK, STB at 380050/51) and reads TP and DATA OUT
 * on bits 6 and 7 of 320001. The BIOS waits for TP edges during boot (C11D6A), so TP timing
 * matters. The timers are evaluated as a function of the 68000 cycle: a periodic "toggle" that
 * starts at t0 changes level at t0, t0 + T, t0 + 2T, ...
 *
 * Crystal clock: 32 768 Hz. Commands (4-bit command register):
 *   0 register hold (DATA OUT = 1 Hz pulse, TP = 64 Hz)       1 shift
 *   2 time set                                                3 time read
 *   4-7 TP = 64/256/2048/4096 Hz                              8-11 TP = 1/10/30/60 s intervals
 */
#include "neogeo.h"

#include <string.h>
#include <time.h>

#define CYCLES_PER_SECOND 12000000LL

typedef struct {
    int64_t t0, period;          /* period <= 0: stopped */
    int base;                    /* level before the first change */
} Pulse;

static struct {
    uint8_t shift[7];            /* 6 time bytes (BCD) + command register */
    uint8_t datetime[6];         /* seconds, minutes, hours, day, month<<4|weekday, year */
    uint8_t command, clk, stb, data_in, data_out_fixed;
    Pulse tp, data_out;
    int data_out_is_pulse;       /* 1: DATA OUT is the pulse; 0: it is the register bit */
    int64_t seconds_since;       /* cycle of the last seconds advance */
    int clock_running;
    int time_set;
} rtc;

void ng_rtc_state(NgState *e) { ng_state_block(e, &rtc, sizeof rtc); }

static int pulse_level(const Pulse *p, int64_t now)
{
    if (p->period <= 0 || now < p->t0) return p->base;
    int64_t changes = 1 + (now - p->t0) / p->period;
    return p->base ^ (int)(changes & 1);
}

static void pulse_start(Pulse *p, int64_t now, int64_t period)
{
    p->base = pulse_level(p, now);
    p->t0 = now;
    p->period = period;
}

static void pulse_stop(Pulse *p, int64_t now)
{
    p->base = pulse_level(p, now);
    p->period = 0;
}

static uint8_t bcd(int v) { return (uint8_t)((v / 10) << 4 | (v % 10)); }
static int from_bcd(uint8_t v) { return (v >> 4) * 10 + (v & 0xF); }

static int days_in_month(int month, int year)
{
    static const int d[12] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};
    if (month == 2 && year % 4 == 0) return 29;
    return (month >= 1 && month <= 12) ? d[month - 1] : 31;
}

static void advance_second(void)
{
    int s = from_bcd(rtc.datetime[0]) + 1;
    if (s < 60) { rtc.datetime[0] = bcd(s); return; }
    rtc.datetime[0] = 0;
    int m = from_bcd(rtc.datetime[1]) + 1;
    if (m < 60) { rtc.datetime[1] = bcd(m); return; }
    rtc.datetime[1] = 0;
    int h = from_bcd(rtc.datetime[2]) + 1;
    if (h < 24) { rtc.datetime[2] = bcd(h); return; }
    rtc.datetime[2] = 0;
    int month = rtc.datetime[4] >> 4, wday = rtc.datetime[4] & 0xF, year = from_bcd(rtc.datetime[5]);
    wday = (wday + 1) % 7;
    int d = from_bcd(rtc.datetime[3]) + 1;
    if (d > days_in_month(month, year)) {
        d = 1;
        if (++month > 12) { month = 1; year = (year + 1) % 100; }
    }
    rtc.datetime[3] = bcd(d);
    rtc.datetime[4] = (uint8_t)(month << 4 | wday);
    rtc.datetime[5] = bcd(year);
}

/* Advances the seconds elapsed up to `now` (the divider runs since power-on). */
static void update(int64_t now)
{
    while (now - rtc.seconds_since >= CYCLES_PER_SECOND) {
        rtc.seconds_since += CYCLES_PER_SECOND;
        if (rtc.clock_running) advance_second();
    }
}

void ng_rtc_set(int year, int month, int day, int weekday, int hour, int minute, int second)
{
    rtc.datetime[0] = bcd(second);
    rtc.datetime[1] = bcd(minute);
    rtc.datetime[2] = bcd(hour);
    rtc.datetime[3] = bcd(day);
    rtc.datetime[4] = (uint8_t)(month << 4 | weekday);
    rtc.datetime[5] = bcd(year % 100);
    rtc.time_set = 1;
}

void ng_rtc_init(void)
{
    int time_set = rtc.time_set;
    uint8_t datetime[6];
    for (int i = 0; i < 6; i++) datetime[i] = rtc.datetime[i];
    memset(&rtc, 0, sizeof rtc);
    if (time_set) {
        for (int i = 0; i < 6; i++) rtc.datetime[i] = datetime[i];
        rtc.time_set = 1;
    } else {
        time_t t = time(NULL);
        struct tm *l = localtime(&t);
        ng_rtc_set(l->tm_year, l->tm_mon + 1, l->tm_mday, l->tm_wday, l->tm_hour, l->tm_min, l->tm_sec);
        rtc.time_set = 0;
    }
    rtc.seconds_since = ng_cpu.cycles;
    rtc.clock_running = 1;
}

static void command(int64_t now)
{
    rtc.command = rtc.shift[6];
    int c = rtc.command;
    if (c == 0 || (c >= 4 && c < 15)) rtc.clock_running = 1;
    switch (c) {
    case 0:
        rtc.data_out_is_pulse = 1;
        pulse_start(&rtc.data_out, now, CYCLES_PER_SECOND / 2);
        pulse_start(&rtc.tp, now, CYCLES_PER_SECOND / 128);
        break;
    case 1:
        pulse_stop(&rtc.data_out, now);
        rtc.data_out_is_pulse = 0;
        break;
    case 2:
        rtc.clock_running = 0;
        pulse_stop(&rtc.data_out, now);
        rtc.data_out_is_pulse = 0;
        for (int i = 0; i < 6; i++) rtc.datetime[i] = rtc.shift[i];
        break;
    case 3:
        rtc.clock_running = 1;
        for (int i = 0; i < 6; i++) rtc.shift[i] = rtc.datetime[i];
        rtc.data_out_is_pulse = 1;
        pulse_start(&rtc.data_out, now, CYCLES_PER_SECOND / 2);
        break;
    case 4: case 5: case 6: case 7: {
        static const int64_t hz[4] = {64, 256, 2048, 4096};
        pulse_start(&rtc.tp, now, CYCLES_PER_SECOND / (hz[c - 4] * 2));
        break;
    }
    case 8: case 9: case 10: case 11: {
        static const int64_t secs[4] = {1, 10, 30, 60};
        pulse_start(&rtc.tp, now, CYCLES_PER_SECOND * secs[c - 8] / 2);
        break;
    }
    default: break;               /* 12-14 interrupts, 15 test: unused */
    }
}

void ng_rtc_write(uint8_t data)
{
    int64_t now = ng_cpu.cycles;
    update(now);
    int din = data & 1, clk = (data >> 1) & 1, stb = (data >> 2) & 1;
    rtc.data_in = (uint8_t)din;
    if (!rtc.clk && clk) {                       /* CLK rising edge */
        int carry_in = rtc.shift[6] & 1;         /* the command register overflows into the time register */
        rtc.shift[6] = (uint8_t)((rtc.shift[6] >> 1) | (din << 3));
        if (rtc.command == 1) {
            for (int i = 0; i < 6; i++) {
                rtc.shift[i] >>= 1;
                if (i == 5) rtc.shift[i] |= (uint8_t)(carry_in << 7);
                else rtc.shift[i] |= (uint8_t)((rtc.shift[i + 1] << 7) & 0x80);
            }
        }
    }
    rtc.clk = (uint8_t)clk;
    if (!rtc.stb && stb) command(now);           /* STB rising edge */
    rtc.stb = (uint8_t)stb;
}

int ng_rtc_tp(void)
{
    return pulse_level(&rtc.tp, ng_cpu.cycles);
}

int ng_rtc_data(void)
{
    update(ng_cpu.cycles);
    if (rtc.data_out_is_pulse) return pulse_level(&rtc.data_out, ng_cpu.cycles);
    return rtc.shift[0] & 1;
}
