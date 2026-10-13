/* Metal Slug X, native port: command-line options and startup.
 *
 * Usage:
 *   mslugx [GAME | --roms GAME] [--scale N] [--turbo N] [--empty-sram] [--no-audio]
 *          [--date YYYY-MM-DD-hh-mm-ss-D]
 *   mslugx --headless --frames N [--screenshots a,b,c --output FOLDER] ...
 *
 * --roms (or just the path, e.g. a folder dropped on the executable): the user's copy, a folder
 *         or a zip: the PC release's install folder (GOG, Steam) or files, or a MAME set
 *         (mslugx + neogeo). Without it, windowed mode looks for it on its own (find_game.c,
 *         window.c); headless mode uses "game" or "roms" next to the executable. Recognized
 *         by hash (neogeo/roms.c).
 * PD_INTERPRETER=1: runs the 68000 in the interpreter only, even with a recompiled program.
 * PD_ROMS_INFO=1 (headless): prints what was loaded and whether the recompiled program is used.
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
#include "hashes.h"
#include "neogeo.h"
#include "port.h"
#include "profile.h"

/* The recompiled program (or the empty one of an interpreter-only build, cpu/program_none.c),
   with the SHA-256 of the P1 and BIOS it was generated from. */
void program_register(void);
extern const char program_p1_sha256[], program_bios_sha256[];

static void usage(void)
{
    fprintf(stderr,
            "Usage: mslugx [GAME | --roms GAME] [--scale N] [--turbo N] [--empty-sram] [--no-audio]\n"
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
        } else if (a[0] != '-' && !o->roms) o->roms = a;      /* path dropped on the executable */
        else return -1;
    }
    if (o->scale < 0 || o->turbo > 2) return -1;
    if (o->headless && o->frames <= 0) return -1;
    return 0;
}

/* Recompiled program, fallback interpreter and machine power-on (with the ROMs already
   loaded into ng_roms). The recompiled program is only used with the exact P1 and BIOS it was
   generated from; any other copy (another version, another BIOS) runs in the interpreter. */
int machine_recompiled(void)
{
    char p1[65], bios[65];
    hex_string(ng_roms.info.p1_sha256, 32, p1);
    hex_string(ng_roms.info.bios_sha256, 32, bios);
    return program_p1_sha256[0] && !strcmp(p1, program_p1_sha256) && !strcmp(bios, program_bios_sha256) &&
           !getenv("PD_INTERPRETER");
}

void machine_start(const Options *o)
{
    static int registered;
    if (!registered) {
        if (machine_recompiled()) program_register();
        else rc_interpreter_only = 1;
        rc_interpreter_init();
        registered = 1;
    }
    if (o->has_date) ng_rtc_set(o->date[0], o->date[1], o->date[2], o->date[6], o->date[3], o->date[4], o->date[5]);
    ng_power_on();
}

/* PD_COMPARE_ROMS=OTHER (headless): loads another copy and checks that the runtime sees exactly
   the same data (for example a MAME set against the PC release files). */
static int compare_roms(const char *other)
{
    NgRoms b;
    char error[1200];
    if (ng_roms_load(&b, other, error, sizeof error) != 0) { fprintf(stderr, "%s\n", error); return 1; }
    printf("%s\n", b.info.description);
    const NgRoms *a = &ng_roms;
    struct { const char *name; const uint8_t *x, *y; uint32_t n; } parts[] = {
        {"p", a->p, b.p, a->p_size}, {"bios", a->bios, b.bios, 131072},
        {"fix_game", a->fix_game, b.fix_game, 131072}, {"fix_bios", a->fix_bios, b.fix_bios, 131072},
        {"tiles", a->tiles, b.tiles, a->n_tiles * 128}, {"zoom", a->zoom, b.zoom, 131072},
        {"z80", a->z80, b.z80, 131072}, {"adpcm", a->adpcm, b.adpcm, a->adpcm_size},
    };
    int differ = a->p_size != b.p_size || a->n_tiles != b.n_tiles || a->adpcm_size != b.adpcm_size;
    for (size_t i = 0; i < sizeof parts / sizeof parts[0] && !differ; i++) {
        int d = memcmp(parts[i].x, parts[i].y, parts[i].n) != 0;
        printf("%-9s %s\n", parts[i].name, d ? "DIFFERENT" : "identical");
        differ |= d;
    }
    ng_roms_free(&b);
    return differ;
}

int main(int argc, char **argv)
{
    Options o;
    if (parse_options(argc, argv, &o) != 0) { usage(); return 2; }
    if (!o.headless) return window_run(&o);

    char folder[1024], error[1200];
    const char *base = SDL_GetBasePath();
    if (o.roms) snprintf(folder, sizeof folder, "%s", o.roms);
    else {
        snprintf(folder, sizeof folder, "%sgame", base ? base : "");
        if (ng_roms_load(&ng_roms, folder, error, sizeof error) == 0) ng_roms_free(&ng_roms);
        else snprintf(folder, sizeof folder, "%sroms", base ? base : "");
    }
    if (ng_roms_load(&ng_roms, folder, error, sizeof error) != 0) {
        fprintf(stderr, "%s\n", error);
        fprintf(stderr, "Pass your copy of the game (a folder or a zip) with --roms.\n");
        return 1;
    }
    if (getenv("PD_ROMS_INFO"))
        printf("%s\n%s\nprofile %s\n", ng_roms.info.description, machine_recompiled() ? "recompiled" : "interpreter",
               profile_supported() ? "supported" : "not supported");
    if (getenv("PD_COMPARE_ROMS")) return compare_roms(getenv("PD_COMPARE_ROMS"));
    cpu_turbo = o.turbo > 0 ? o.turbo : 0;
    if (headless_prepare(&o) != 0) return 1;
    machine_start(&o);
    return headless_run(&o);
}
