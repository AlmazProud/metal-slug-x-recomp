/* YM2610 probe, port side: plays a tools/mame/ym_probe.lua script through the port's YM2610
 * (src/neogeo/ym2610.cpp + ymfm) and writes the audio as the runtime mixes it, to compare with
 * MAME's -wavwrite of the same script.
 *
 * Usage: ym_probe ADPCM_ROM SCRIPT LOG OUT.wav
 *   ADPCM_ROM  the V ROM (mslug3_adpcm)
 *   SCRIPT     the probe script ("<frame> <bank> <reg> <value>", "suspend" lines ignored)
 *   LOG        ym_probe.lua's log ("<frame> <time us> <pc>"): the time of each frame's writes
 * The chip starts reset at time 0. Each frame's writes apply one sample after the first sample
 * that ends after their time (the runtime's sample clock, SAMPLE_PHASE included): MAME applies
 * the writes the Lua script makes from its frame notifier one sample later than the same writes
 * made by the Z80 (measured: the ADPCM-B model of the ymfm copy matches both the probes and the
 * game's own Z80 writes only this way). PROBE_PHASE: see main.
 */
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "ym2610.h"

#define SAMPLE_HZ 55555
#define MASTER_HZ 24000000
#define SAMPLE_PHASE 6

static uint8_t *rom;
static uint32_t rom_size;

void ng_ym_timer(uint32_t n, int32_t cycles) { (void)n; (void)cycles; }
void ng_ym_busy_until(uint32_t cycles) { (void)cycles; }
int ng_ym_busy(void) { return 0; }
void ng_ym_irq(int active) { (void)active; }
uint8_t ng_ym_rom(uint32_t a) { a &= 0xFFFFFF; return a < rom_size ? rom[a] : 0; }

static int16_t saturate(float v) { return (int16_t)(v > 32767 ? 32767 : v < -32768 ? -32768 : v); }

typedef struct { long frame; int bank, reg, val; } Write;

int main(int argc, char **argv)
{
    if (argc != 5) { fprintf(stderr, "usage: ym_probe ADPCM_ROM SCRIPT LOG OUT.wav\n"); return 2; }
    FILE *f = fopen(argv[1], "rb");
    if (!f) return 1;
    fseek(f, 0, SEEK_END); rom_size = (uint32_t)ftell(f); fseek(f, 0, SEEK_SET);
    rom = malloc(rom_size);
    if (fread(rom, 1, rom_size, f) != rom_size) return 1;
    fclose(f);

    static Write w[100000];
    int nw = 0;
    char line[256];
    f = fopen(argv[2], "r");
    while (f && fgets(line, sizeof line, f) && nw < 100000) {
        long fr; int b; unsigned r, v;
        if (sscanf(line, "%ld %d %x %x", &fr, &b, &r, &v) == 4) w[nw++] = (Write){fr, b, (int)r, (int)v};
    }
    if (f) fclose(f);
    static long log_frame[10000]; static double log_us[10000];
    int nl = 0;
    f = fopen(argv[3], "r");
    while (f && fgets(line, sizeof line, f) && nl < 10000)
        if (sscanf(line, "%ld %lf", &log_frame[nl], &log_us[nl]) == 2) nl++;
    if (f) fclose(f);

    double last_us = log_us[nl - 1] + 3e6;
    long n_samples = (long)(last_us * 1e-6 * SAMPLE_HZ);
    FILE *o = fopen(argv[4], "wb");
    uint32_t bytes = (uint32_t)n_samples * 4, v32;
    fwrite("RIFF", 1, 4, o); v32 = 36 + bytes; fwrite(&v32, 4, 1, o);
    fwrite("WAVEfmt ", 1, 8, o); v32 = 16; fwrite(&v32, 4, 1, o);
    uint16_t v16 = 1; fwrite(&v16, 2, 1, o); v16 = 2; fwrite(&v16, 2, 1, o);
    v32 = SAMPLE_HZ; fwrite(&v32, 4, 1, o); v32 = SAMPLE_HZ * 4; fwrite(&v32, 4, 1, o);
    v16 = 4; fwrite(&v16, 2, 1, o); v16 = 16; fwrite(&v16, 2, 1, o);
    fwrite("data", 1, 4, o); fwrite(&bytes, 4, 1, o);

    ym_reset();
    /* PROBE_PHASE=k: k chip samples before time 0 (the ADPCM-A clock phase depends on how long
       the chip has run since its reset) */
    for (int k = getenv("PROBE_PHASE") ? atoi(getenv("PROBE_PHASE")) : 0; k > 0; k--) {
        int32_t dummy[3];
        ym_generate(dummy);
    }
    int next_log = 0;
    for (long k = 0; k < n_samples; k++) {
        /* sample k - 1 ends at this master cycle; the probe's writes made before it apply to
           sample k (see above) */
        long long end = ((long long)k * MASTER_HZ + SAMPLE_HZ - 1) / SAMPLE_HZ + SAMPLE_PHASE;
        while (next_log < nl && log_us[next_log] * 24.0 < (double)end) {
            for (int i = 0; i < nw; i++)
                if (w[i].frame == log_frame[next_log]) {
                    ym_write(w[i].bank * 2, (uint8_t)w[i].reg);
                    ym_write(w[i].bank * 2 + 1, (uint8_t)w[i].val);
                }
            next_log++;
        }
        int32_t out[3];
        ym_generate(out);
        float fm_l = (float)out[0] / 32768.0f, fm_r = (float)out[1] / 32768.0f, ssg = (float)out[2] / 32768.0f;
        int16_t s[2] = {saturate((fm_l * 0.98f + ssg * 0.84f) * 32768.0f), saturate((fm_r * 0.98f + ssg * 0.84f) * 32768.0f)};
        fwrite(s, 2, 2, o);
    }
    fclose(o);
    return 0;
}
