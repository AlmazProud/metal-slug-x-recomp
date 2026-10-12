/* Metal Slug X, native port: command-line options and startup.
 *
 * Usage:
 *   mslugx [--roms FOLDER] [--scale N] [--turbo N] [--empty-sram] [--no-audio]
 *          [--date YYYY-MM-DD-hh-mm-ss-D]
 *   mslugx --headless --frames N [--screenshots a,b,c --output FOLDER] ...
 *
 * --roms: resources\game folder of the GOG installation. Without this option, windowed mode
 *         looks for it on its own (window.c); headless mode uses "roms" next to the executable.
 *         The files are validated by SHA-256.
 * --turbo: 0 = like the original; 1 / 2 = 68000 at double / quadruple speed (without the
 *         original slowdown). In windowed mode, the value from mslugx.ini is the default.
 * --headless: runs N frames as fast as possible (verification against MAME, headless.c):
 *   --screenshots: saves BMPs of the requested frames (tools/compare_screenshots.py).
 *   --wav: saves the generated audio (stereo, 16 bits, 55 555 Hz).
 *   --inputs: inputs recorded in MAME (tools/mame/inputs.lua, timed "C" lines,
 *         or "E frame P1 P2 SIS MON TEST"). --fingerprints: per-frame and per-VBlank fingerprints (Phase 4).
 *   --script: inputs by spans: "from to input..." (frames inclusive; inputs: up down left
 *         right a b c d start1 start2 select1 coin1 coin2 test).
 *   --trace, --trace-io: diagnostic traces.
 *   --test-state M: at the end saves a state, runs M frames, resets the machine, loads the
 *         state and repeats: RAM, image and audio must match (save-state test).
 *   --sound-catalog FILE: after the N frames (sound driver started, silent), sends each sound
 *         command 00-FF separately and records duration, loop, peak and notes per channel
 *         type. With --output, also saves snd_XX.wav for each one (Phase 6B).
 *   --test-sounds: at the end opens the sound menu, plays a music track and a voice (they
 *         must be audible), closes it and checks that the game is identical to not having opened it.
 *
 * Controls, menu and fixed keys of windowed mode: see window.c.
 */
#include <SDL3/SDL.h>
#include <SDL3/SDL_main.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dispatcher.h"
#include "neogeo.h"
#include "port.h"

void program_register(void);

static void usage(void)
{
    fprintf(stderr,
            "Usage: mslugx [--roms FOLDER] [--scale N] [--turbo N] [--empty-sram] [--no-audio]\n"
            "              [--date YYYY-MM-DD-hh-mm-ss-D]\n"
            "       mslugx --headless --frames N [--screenshots a,b,c --output FOLDER] [--script FILE]\n"
            "              [--inputs FILE] [--fingerprints FILE] [--wav FILE] [--trace FILE]\n"
            "              [--trace-io FILE FROM_US TO_US]\n");
}

static int parse_options(int argc, char **argv, Options *o)
{
    memset(o, 0, sizeof *o);
    o->turbo = -1;
    for (int i = 1; i < argc; i++) {
        const char *a = argv[i];
        const char *v = i + 1 < argc ? argv[i + 1] : NULL;
        if (!strcmp(a, "--roms") && v) { o->roms = v; i++; }
        else if (!strcmp(a, "--scale") && v) { o->scale = atoi(v); i++; }
        else if (!strcmp(a, "--turbo") && v) { o->turbo = atoi(v); i++; }
        else if (!strcmp(a, "--frames") && v) { o->frames = atol(v); i++; }
        else if (!strcmp(a, "--screenshots") && v) { o->screenshots = v; i++; }
        else if (!strcmp(a, "--output") && v) { o->output = v; i++; }
        else if (!strcmp(a, "--script") && v) { o->script = v; i++; }
        else if (!strcmp(a, "--wav") && v) { o->wav = v; i++; }
        else if (!strcmp(a, "--inputs") && v) { o->inputs = v; i++; }
        else if (!strcmp(a, "--fingerprints") && v) { o->fingerprints = v; i++; }
        else if (!strcmp(a, "--dump") && v) { o->dump = v; i++; }
        else if (!strcmp(a, "--no-audio")) o->no_audio = 1;
        else if (!strcmp(a, "--test-state") && v) { o->test_state = atoi(v); i++; }
        else if (!strcmp(a, "--sound-catalog") && v) { o->catalog = v; i++; }
        else if (!strcmp(a, "--test-sounds")) o->test_sounds = 1;
        else if (!strcmp(a, "--blood")) o->blood = 1;
        else if (!strcmp(a, "--trace") && v) { ng_trace = fopen(v, "w"); i++; }
        else if (!strcmp(a, "--trace-io") && i + 3 < argc) {
            /* --trace-io FILE FROM_US TO_US */
            ng_trace_io = fopen(argv[i + 1], "w");
            ng_trace_from = (int64_t)(atof(argv[i + 2]) * 12.0);
            ng_trace_to = (int64_t)(atof(argv[i + 3]) * 12.0);
            i += 3;
        }
        else if (!strcmp(a, "--headless")) o->headless = 1;
        else if (!strcmp(a, "--empty-sram")) o->empty_sram = 1;
        else if (!strcmp(a, "--date") && v) {
            int *f = o->date;
            if (sscanf(v, "%d-%d-%d-%d-%d-%d-%d", &f[0], &f[1], &f[2], &f[3], &f[4], &f[5], &f[6]) != 7) return -1;
            o->has_date = 1;
            i++;
        } else return -1;
    }
    if (o->scale < 0 || o->turbo > 2) return -1;
    if (o->headless && o->frames <= 0) return -1;
    return 0;
}

/* Recompiled program, fallback interpreter and machine power-on (with the ROMs already
   loaded into ng_roms). */
void machine_start(const Options *o)
{
    static int registered;
    if (!registered) { program_register(); rc_interpreter_init(); registered = 1; }
    if (o->has_date) ng_rtc_set(o->date[0], o->date[1], o->date[2], o->date[6], o->date[3], o->date[4], o->date[5]);
    ng_power_on();
}

int main(int argc, char **argv)
{
    Options o;
    if (parse_options(argc, argv, &o) != 0) { usage(); return 2; }
    if (!o.headless) return window_run(&o);

    char folder[1024];
    if (o.roms) snprintf(folder, sizeof folder, "%s", o.roms);
    else {
        const char *base = SDL_GetBasePath();
        snprintf(folder, sizeof folder, "%sroms", base ? base : "");
    }
    char error[1200];
    if (ng_roms_load(&ng_roms, folder, error, sizeof error) != 0) {
        fprintf(stderr, "%s\n", error);
        fprintf(stderr, "Pass the resources\\game folder of your GOG installation with --roms.\n");
        return 1;
    }
    cpu_turbo = o.turbo > 0 ? o.turbo : 0;
    if (headless_prepare(&o) != 0) return 1;
    machine_start(&o);
    return headless_run(&o);
}
