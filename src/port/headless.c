/* Headless mode: deterministic runs to verify against MAME (Phases 3 and 4).
 *
 * BMP screenshots, input script, inputs recorded in MAME (per frame or timed), fingerprints
 * per frame and per VBlank, per-instruction traces and WAV of the generated audio.
 */
#include <SDL3/SDL.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dispatcher.h"
#include "neogeo.h"
#include "cheats.h"
#include "profile.h"
#include "port.h"
#include "sounds.h"

/* ------------------------------------------------------------------ input script */
typedef struct { unsigned long long from, to; uint8_t joy, sys[2], test; } Span;
static Span *spans;
static size_t n_spans;

/* Tokens are English; the old Spanish ones are still accepted as aliases. */
static int is_token(const char *tok, const char *en, const char *es)
{
    return !strcmp(tok, en) || (es && !strcmp(tok, es));
}

static int script_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Could not open script %s\n", path); return -1; }
    char line[512];
    while (fgets(line, sizeof line, f)) {
        if (line[0] == '#') continue;
        Span sp = {0};
        int used = 0;
        if (sscanf(line, "%llu %llu %n", &sp.from, &sp.to, &used) < 2) continue;
        for (char *tok = strtok(line + used, " \t\r\n"); tok; tok = strtok(NULL, " \t\r\n")) {
            if (is_token(tok, "up", "arriba")) sp.joy |= NG_UP;
            else if (is_token(tok, "down", "abajo")) sp.joy |= NG_DOWN;
            else if (is_token(tok, "left", "izquierda")) sp.joy |= NG_LEFT;
            else if (is_token(tok, "right", "derecha")) sp.joy |= NG_RIGHT;
            else if (is_token(tok, "a", NULL)) sp.joy |= NG_A;
            else if (is_token(tok, "b", NULL)) sp.joy |= NG_B;
            else if (is_token(tok, "c", NULL)) sp.joy |= NG_C;
            else if (is_token(tok, "d", NULL)) sp.joy |= NG_D;
            else if (is_token(tok, "start1", NULL)) sp.sys[0] |= NG_START;
            else if (is_token(tok, "start2", NULL)) sp.sys[1] |= NG_START;
            else if (is_token(tok, "select1", NULL)) sp.sys[0] |= NG_SELECT;
            else if (is_token(tok, "coin1", "moneda1")) sp.sys[0] |= NG_COIN;
            else if (is_token(tok, "coin2", "moneda2")) sp.sys[1] |= NG_COIN;
            else if (is_token(tok, "test", NULL)) sp.test = 1;
            else { fprintf(stderr, "Unknown input in script: %s\n", tok); fclose(f); return -1; }
        }
        spans = realloc(spans, (n_spans + 1) * sizeof *spans);
        spans[n_spans++] = sp;
    }
    fclose(f);
    return 0;
}

/* Inputs for the frame about to run: ng.frames, with MAME's numbering. */
static void script_apply(void)
{
    uint8_t joy = 0, s0 = 0, s1 = 0, test = 0;
    for (size_t i = 0; i < n_spans; i++)
        if (ng.frames >= spans[i].from && ng.frames <= spans[i].to) {
            joy |= spans[i].joy; s0 |= spans[i].sys[0]; s1 |= spans[i].sys[1]; test |= spans[i].test;
        }
    ng.joy[0] = joy;
    ng.system[0] = s0;
    ng.system[1] = s1;
    ng.test = test ? 0x00 : 0x80;
}

/* PD_CHEAT=frame,kind,player,value[;...]: cheats applied before that frame (tests). kind:
   weapon, bombs, lives, soldier, form, vehicle, spawntype, devmenu (poke: research). */
static void cheats_test(void)
{
    const char *e = getenv("PD_CHEAT");
    while (e && *e) {
        unsigned long long frame;
        char kind[16];
        int player, value, used = 0;
        if (sscanf(e, "%llu,%15[a-z],%d,%d%n", &frame, kind, &player, &value, &used) != 4) return;
        if (frame == ng.frames) {
            int ok = 1;
            if (!strcmp(kind, "weapon")) ok = cheats_give_weapon(player, value);
            else if (!strcmp(kind, "bombs")) ok = cheats_set_bombs(player, value);
            else if (!strcmp(kind, "lives")) ok = cheats_set_lives(player, value);
            else if (!strcmp(kind, "soldier")) ok = cheats_set_soldier(player, value);
            else if (!strcmp(kind, "form")) ok = cheats_set_form(player, value);
            else if (!strcmp(kind, "vehicle")) ok = cheats_spawn_vehicle(value);
            else if (!strcmp(kind, "spawntype")) ok = cheats_spawn_type(value);
            else if (!strcmp(kind, "devmenu")) cheats_dev_menu();
            else if (!strcmp(kind, "poke"))       /* research: player object byte, value = off*256+v */
                ng.ram[((player ? PROFILE_PLAYER2_OBJECT : PROFILE_PLAYER1_OBJECT) + (value >> 8)) & 0xFFFF] = (uint8_t)value;
            if (!ok) fprintf(stderr, "PD_CHEAT: %s not applied at frame %llu (not in game)\n", kind, frame);
        }
        e += used;
        if (*e == ';') e++;
    }
}

/* ------------------------------------------------------------------ BMP screenshots */
int save_bmp(const char *path)
{
    FILE *f = fopen(path, "wb");
    if (!f) return -1;
    const uint32_t row = NG_WIDTH * 3, data = row * NG_HEIGHT, total = 54 + data;
    uint8_t h[54] = {'B', 'M'};
    #define P32(o, v) do { h[o] = (uint8_t)(v); h[o + 1] = (uint8_t)((v) >> 8); h[o + 2] = (uint8_t)((v) >> 16); h[o + 3] = (uint8_t)((v) >> 24); } while (0)
    P32(2, total); P32(10, 54); P32(14, 40); P32(18, NG_WIDTH); P32(22, NG_HEIGHT);
    h[26] = 1; h[28] = 24; P32(34, data);
    #undef P32
    fwrite(h, 1, 54, f);
    uint8_t line[NG_WIDTH * 3];
    for (int y = NG_HEIGHT - 1; y >= 0; y--) {
        for (int x = 0; x < NG_WIDTH; x++) {
            uint32_t p = ng.screen[y][x];
            line[3 * x] = (uint8_t)p; line[3 * x + 1] = (uint8_t)(p >> 8); line[3 * x + 2] = (uint8_t)(p >> 16);
        }
        fwrite(line, 1, sizeof line, f);
    }
    fclose(f);
    return 0;
}

/* ------------------------------------------------------------------ timed inputs */
/* Input changes recorded in MAME (tools/mame/inputs.lua): "C t0 t1 PUERTO VALOR",
   with t0 = last read with the old value and t1 = first read with the new one (us). The change
   is applied at the midpoint, in 68000 cycles. */
typedef struct { int64_t cycle; int port; uint8_t value; } InputChange;
static InputChange *changes;
static size_t n_changes, i_change;

static int changes_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) return -1;
    char line[256], name[16];
    double t0, t1;
    unsigned v;
    while (fgets(line, sizeof line, f)) {
        if (sscanf(line, "C %lf %lf %15s %x", &t0, &t1, name, &v) != 4) continue;
        int p = !strcmp(name, "P1") ? 0 : !strcmp(name, "P2") ? 1 : !strcmp(name, "SIS") ? 2 :
                !strcmp(name, "MON") ? 3 : !strcmp(name, "TEST") ? 4 : -1;
        if (p < 0) continue;
        changes = realloc(changes, (n_changes + 1) * sizeof *changes);
        changes[n_changes++] = (InputChange){(int64_t)((t0 + t1) / 2.0 * 12.0), p, (uint8_t)v};
    }
    fclose(f);
    return 0;
}

static void changes_apply(void)
{
    while (i_change < n_changes && changes[i_change].cycle <= ng_cpu.cycles) {
        uint8_t v = changes[i_change].value;
        switch (changes[i_change].port) {
        case 0: ng.raw_p1 = v; break;
        case 1: ng.raw_p2 = v; break;
        case 2: ng.raw_system = v; break;
        case 3: ng.raw_coins = v; break;
        default: ng.raw_test = v; break;
        }
        i_change++;
    }
}

/* ------------------------------------------------------------------ raw inputs and fingerprints */
typedef struct { uint8_t v[5]; uint8_t present; } RawInput;
static RawInput *raws;
static size_t n_raws;

static int inputs_load(const char *path)
{
    FILE *f = fopen(path, "r");
    if (!f) { fprintf(stderr, "Could not open %s\n", path); return -1; }
    char line[256];
    while (fgets(line, sizeof line, f)) {
        unsigned long long n;
        unsigned v[5];
        if (sscanf(line, "E %llu %x %x %x %x %x", &n, &v[0], &v[1], &v[2], &v[3], &v[4]) != 6) continue;
        if (n >= n_raws) {
            size_t new_size = (size_t)n + 1024;
            raws = realloc(raws, new_size * sizeof *raws);
            memset(raws + n_raws, 0, (new_size - n_raws) * sizeof *raws);
            n_raws = new_size;
        }
        for (int i = 0; i < 5; i++) raws[n].v[i] = (uint8_t)v[i];
        raws[n].present = 1;
    }
    fclose(f);
    /* The same file may also contain timed changes ("C" lines): they take priority. */
    if (changes_load(path) == 0 && n_changes) {
        free(raws); raws = NULL; n_raws = 0;
        ng_before_input_read = changes_apply;
    }
    ng.raw = 1;
    ng.raw_p1 = ng.raw_p2 = ng.raw_system = 0xFF;
    ng.raw_coins = 0x1F;
    ng.raw_test = 0x80;
    return 0;
}

/* A frame's inputs hold until another frame changes them. */
static void inputs_apply(void)
{
    if (!raws || ng.frames >= n_raws || !raws[ng.frames].present) return;
    const uint8_t *v = raws[ng.frames].v;
    ng.raw_p1 = v[0]; ng.raw_p2 = v[1]; ng.raw_system = v[2];
    ng.raw_coins = v[3]; ng.raw_test = v[4];
}

static uint32_t fnv_bytes(uint32_t h, const uint8_t *d, size_t n)
{
    for (size_t i = 0; i < n; i++) h = (h ^ d[i]) * 0x01000193u;
    return h;
}

/* PD_DUMP_SPRITES=frame,file: list of the sprites drawn in that frame (box in visible pixels
   and SCB2-4 registers, first tile). Diagnostic for locating the game's graphics. */
static void dump_sprites(void)
{
    const char *e = getenv("PD_DUMP_SPRITES");
    if (!e) return;
    unsigned long long frame;
    char path[1024];
    if (sscanf(e, "%llu,%1023s", &frame, path) != 2) return;
    ng_dbg.active = 1;
    if (ng.frames - 1 != frame) return;
    FILE *f = fopen(path, "w");
    if (!f) return;
    for (int s = 0; s < 381; s++) {
        const NgBox *c = &ng_dbg.box[s];
        if (c->x1 < c->x0) continue;
        fprintf(f, "%3d box %4d,%4d-%4d,%4d scb2 %04X scb3 %04X scb4 %04X tile0 %05X attr %04X\n", s, c->x0, c->y0,
                c->x1, c->y1, ng.vram[0x8000 | s], ng.vram[0x8200 | s], ng.vram[0x8400 | s],
                ((ng.vram[(s << 6) + 1] << 12) & 0xF0000) | ng.vram[s << 6], ng.vram[(s << 6) + 1]);
    }
    fclose(f);
}

/* PD_CAPTURE_SPRITES=frame,from,to,file.bmp: image with only those sprites (magenta background). */
static void capture_sprites(void)
{
    const char *e = getenv("PD_CAPTURE_SPRITES");
    if (!e) return;
    unsigned long long frame;
    int from, to;
    char path[1024];
    if (sscanf(e, "%llu,%d,%d,%1023s", &frame, &from, &to, path) != 4 || ng.frames - 1 != frame) return;
    static uint32_t img[NG_HEIGHT][NG_WIDTH];
    uint32_t copy[NG_HEIGHT][NG_WIDTH];
    ng_video_capture_sprites(&img[0][0], from, to);
    memcpy(copy, ng.screen, sizeof copy);
    for (int y = 0; y < NG_HEIGHT; y++)
        for (int x = 0; x < NG_WIDTH; x++) ng.screen[y][x] = img[y][x] ? img[y][x] : 0xFFFF00FF;
    save_bmp(path);
    memcpy(ng.screen, copy, sizeof copy);
}

static FILE *fingerprint_file;
static unsigned long long n_vblanks;

static void fingerprint_vblank(void)
{
    uint8_t pal[0x2000];
    const uint16_t *bank = ng.palette + ((uint32_t)ng.palette_bank << 12);
    for (int i = 0; i < 0x1000; i++) { pal[2 * i] = (uint8_t)(bank[i] >> 8); pal[2 * i + 1] = (uint8_t)bank[i]; }
    fprintf(fingerprint_file, "V %llu %08X %08X f=%llu pc=%06X int=%06X a5=%06X l=%02X\n", ++n_vblanks, fnv_bytes(0x811C9DC5u, ng.ram, sizeof ng.ram),
            fnv_bytes(0x811C9DC5u, pal, sizeof pal), (unsigned long long)ng.frames, ng_cpu.pc, ng_pc_vblank,
            ng_cpu.a[5], ng.ram[0x008A]);
    const char *list = getenv("PD_DUMP_V");
    if (list) {
        for (const char *p = list; *p;) {
            char *end;
            unsigned long long v = strtoull(p, &end, 10);
            if (end == p) break;
            if (v == n_vblanks) {
                char path[1200];
                snprintf(path, sizeof path, "%s/ram_port_v%06llu.bin", getenv("PD_DUMP_DIR") ? getenv("PD_DUMP_DIR") : ".", v);
                FILE *f = fopen(path, "wb");
                if (f) { fwrite(ng.ram, 1, sizeof ng.ram, f); fclose(f); }
            }
            p = *end == ',' ? end + 1 : end;
        }
    }
}

static void fingerprint(FILE *f, unsigned long long label)
{
    /* Image: same hash as fingerprints.lua (64-bit words, h = h * 31 + v). */
    uint64_t hp = 0;
    const uint8_t *px = (const uint8_t *)ng.screen;
    for (size_t i = 0; i < sizeof ng.screen; i += 8) {
        uint64_t v;
        memcpy(&v, px + i, 8);
        hp = hp * 31u + v;
    }
    fprintf(f, "F %llu %016llX\n", label, (unsigned long long)hp);
    uint8_t pal[0x2000];
    const uint16_t *bank = ng.palette + ((uint32_t)ng.palette_bank << 12);
    for (int i = 0; i < 0x1000; i++) { pal[2 * i] = (uint8_t)(bank[i] >> 8); pal[2 * i + 1] = (uint8_t)bank[i]; }
    fprintf(f, "%llu %08X %08X\n", label, fnv_bytes(0x811C9DC5u, ng.ram, sizeof ng.ram),
            fnv_bytes(0x811C9DC5u, pal, sizeof pal));
    if (getenv("PD_REGISTERS")) {
        fprintf(f, "R %llu %06X %04X", label, ng_cpu.pc, cpu_get_sr(&ng_cpu));
        for (int i = 0; i < 8; i++) fprintf(f, " %08X", ng_cpu.d[i]);
        for (int i = 0; i < 8; i++) fprintf(f, " %08X", ng_cpu.a[i]);
        fprintf(f, " exceso=%lld\n", (long long)(ng_cpu.cycles - (ng.frame_start + (int64_t)NG_VBLANK_LINE * NG_CYCLES_PER_LINE)));
    }
}

/* ------------------------------------------------------------------ per-instruction trace */
/* PD_TRACE_INSTR=file,from,to (68000 cycles): same format as tools/mame/cycles.lua
   ("C=<cycle> <PC>:"), with everything executed by the interpreter. */
static FILE *instr_trace;
static long long trace_from, trace_to;
static int trace_regs;          /* PD_TRACE_REGS=1: appends D0-D7 to every line */

static void trace_instr(const Cpu68k *c)
{
    if (c->cycles >= trace_from && c->cycles <= trace_to) {
        fprintf(instr_trace, "C=%lld %06X:", (long long)c->cycles, c->pc);
        if (trace_regs)
            for (int i = 0; i < 8; i++) fprintf(instr_trace, " D%d=%08X", i, c->d[i]);
        fputc('\n', instr_trace);
    }
}

/* PD_COUNT_PC=hex: counts per frame how many times that address is executed (all through the
   interpreter). With the exit of the game's wait loop (0x3A42 in mslugx) it measures slowdown:
   a frame without a single iteration of the main loop is a lost frame. */
static uint32_t count_pc;
static unsigned long long count_total, count_frame, frames_without_loop, frames_counted;

static void count_instr(const Cpu68k *c)
{
    if (c->pc == count_pc) count_frame++;
}

static void count_end_frame(void)
{
    if (!count_pc) return;
    frames_counted++;
    count_total += count_frame;
    if (!count_frame) frames_without_loop++;
    static FILE *detail;                    /* PD_COUNT_FILE: one digit per frame */
    if (!detail && getenv("PD_COUNT_FILE")) detail = fopen(getenv("PD_COUNT_FILE"), "w");
    if (detail) fputc(count_frame > 9 ? '+' : (int)('0' + count_frame), detail);
    count_frame = 0;
}

/* PD_WATCH_PC=hex[,first_frame]: logs every execution of that address (frame, return address,
   D0-D7, A0-A6) to stdout, all through the interpreter (research). */
static uint32_t watch_pc;
static unsigned long long watch_from;

static void watch_instr(const Cpu68k *c)
{
    if (c->pc != watch_pc || ng.frames < watch_from) return;
    uint32_t sp = c->a[7];
    printf("W f=%llu ret=%06X", (unsigned long long)ng.frames, bus_r32(sp));
    for (int i = 0; i < 8; i++) printf(" D%d=%08X", i, c->d[i]);
    for (int i = 0; i < 7; i++) printf(" A%d=%08X", i, c->a[i]);
    printf(" [A0]=");
    for (int i = 0; i < 24; i += 2) printf("%04X", bus_r16(c->a[0] + i) & 0xFFFF);
    printf("\n");
}

static void instr_trace_init(void)
{
    if (getenv("PD_WATCH_PC")) {
        sscanf(getenv("PD_WATCH_PC"), "%x,%llu", &watch_pc, &watch_from);
        rc_interpreter_only = 1;
        rc_trace = watch_instr;
    }
    const char *cp = getenv("PD_COUNT_PC");
    if (cp) {
        count_pc = (uint32_t)strtoul(cp, NULL, 16);
        rc_interpreter_only = 1;
        rc_trace = count_instr;
    }
    const char *e = getenv("PD_TRACE_INSTR");
    if (!e) return;
    char path[1024];
    if (sscanf(e, "%1023[^,],%lld,%lld", path, &trace_from, &trace_to) != 3) return;
    instr_trace = fopen(path, "w");
    if (!instr_trace) return;
    rc_interpreter_only = 1;
    trace_regs = getenv("PD_TRACE_REGS") != NULL;
    rc_trace = trace_instr;
}

/* ------------------------------------------------------------------ WAV */
static FILE *wav;
static uint32_t wav_samples;

static void wav_header(FILE *f, uint32_t samples)
{
    uint32_t data = samples * 4, hz = NG_AUDIO_HZ, bps = hz * 4;
    uint8_t h[44] = {'R', 'I', 'F', 'F', 0, 0, 0, 0, 'W', 'A', 'V', 'E', 'f', 'm', 't', ' ',
                     16, 0, 0, 0, 1, 0, 2, 0, 0, 0, 0, 0, 0, 0, 0, 0, 4, 0, 16, 0,
                     'd', 'a', 't', 'a', 0, 0, 0, 0};
    #define P32(o, v) do { h[o] = (uint8_t)(v); h[o + 1] = (uint8_t)((v) >> 8); h[o + 2] = (uint8_t)((v) >> 16); h[o + 3] = (uint8_t)((v) >> 24); } while (0)
    P32(4, 36 + data); P32(24, hz); P32(28, bps); P32(40, data);
    #undef P32
    fseek(f, 0, SEEK_SET);
    fwrite(h, 1, sizeof h, f);
    fseek(f, 0, SEEK_END);
}

static void wav_flush(void)
{
    int16_t b[4096 * 2];
    uint32_t n;
    while ((n = ng_audio_read(b, 4096)) > 0) {
        fwrite(b, 4, n, wav);
        wav_samples += n;
    }
}

static int requested(const char *list, unsigned long long frame)
{
    if (!list) return 0;
    for (const char *p = list; *p;) {
        char *end;
        unsigned long long v = strtoull(p, &end, 10);
        if (end == p) return 0;
        if (v == frame) return 1;
        p = *end == ',' ? end + 1 : end;
    }
    return 0;
}

/* Fingerprint of M frames from the current state: RAM, image and audio of every frame. */
static uint32_t fingerprint_span(const Options *o, int m)
{
    uint32_t h = 0x811C9DC5u;
    int16_t b[4096 * 2];
    for (int n = 0; n < m; n++) {
        script_apply();
        ng_run_frame();
        h = fnv_bytes(h, ng.ram, sizeof ng.ram);
        h = fnv_bytes(h, (const uint8_t *)ng.screen, sizeof ng.screen);
        uint32_t k;
        while ((k = ng_audio_read(b, 4096)) > 0) h = fnv_bytes(h, (const uint8_t *)b, k * 4);
    }
    (void)o;
    return h;
}

static int test_state(const Options *o)
{
    ng_before_input_read = NULL;      /* inputs stay fixed during the span */
    int16_t discard[4096 * 2];
    while (ng_audio_read(discard, 4096) > 0) {}   /* audio from before the state does not count */
    size_t size;
    uint8_t *state = ng_state_save(&size);
    if (!state) return 1;
    uint32_t a = fingerprint_span(o, o->test_state);
    ng_power_on();                         /* another machine: nothing can survive outside the state */
    fingerprint_span(o, 3);
    if (ng_state_load(state, size) != 0) { printf("state rejected\n"); return 1; }
    uint32_t b = fingerprint_span(o, o->test_state);
    free(state);
    printf("state: %zu bytes; %d frames after saving %08X, after loading %08X: %s\n", size, o->test_state,
           a, b, a == b ? "IDENTICAL" : "DIFFERENT");
    return a == b ? 0 : 1;
}

/* Catalog of the sound commands (Phase 6B): each command, alone, from the same state. */
static int sound_catalog(const Options *o)
{
    FILE *f = fopen(o->catalog, "w");
    if (!f) return 1;
    size_t size;
    uint8_t *base = ng_state_save(&size);
    if (!base) { fclose(f); return 1; }
    fprintf(f, "# cmd\tseconds\tloop\tpeak\tfm\tssg\tadpcm_a\tadpcm_b\n");
    const long MAX_SAMPLES = 40L * NG_AUDIO_HZ;          /* looping music is cut off here */
    int16_t b[4096 * 2];
    for (int c = 0; c < 256; c++) {
        ng_state_load(base, size);
        while (ng_audio_read(b, 4096) > 0) {}
        NgNotes n0 = ng_notes;
        FILE *w = NULL;
        if (o->output) {
            char path[1200];
            snprintf(path, sizeof path, "%s/snd_%02X.wav", o->output, c);
            w = fopen(path, "wb");
            if (w) wav_header(w, 0);
        }
        /* Preceding commands, comma separated (hex), one frame between each. The mslugx driver
           uses mode prefixes: 00 = music, 10 = effects, 11 = voices. */
        for (const char *p = getenv("PD_CATALOG_PREFIX"); p && *p;) {
            char *end;
            unsigned long v = strtoul(p, &end, 16);
            if (end == p) break;
            ng_sound_only_command((uint8_t)v);
            ng_sound_only_advance(NG_CYCLES_PER_FRAME);
            while (ng_audio_read(b, 4096) > 0) {}
            p = *end == ',' ? end + 1 : end;
        }
        ng_sound_only_command((uint8_t)c);
        long samples = 0, last = -1;
        int peak = 0;
        for (;;) {
            ng_sound_only_advance(NG_CYCLES_PER_FRAME);
            uint32_t k;
            while ((k = ng_audio_read(b, 4096)) > 0) {
                for (uint32_t i = 0; i < 2 * k; i++) {
                    int v = b[i] < 0 ? -b[i] : b[i];
                    if (v > peak) peak = v;
                    if (v > 64) last = samples + (long)(i / 2);
                }
                if (w) fwrite(b, 4, k, w);
                samples += (long)k;
            }
            if (samples >= MAX_SAMPLES) break;
            if (last < 0 && samples > NG_AUDIO_HZ) break;                 /* nothing within 1 s */
            if (last >= 0 && samples - last > 3L * NG_AUDIO_HZ) break;    /* 3 s of silence */
        }
        if (w) { wav_header(w, (uint32_t)samples); fclose(w); }
        int loops = samples >= MAX_SAMPLES && last > MAX_SAMPLES - NG_AUDIO_HZ;
        fprintf(f, "%02X\t%.2f\t%d\t%d\t%u\t%u\t%u\t%u\n", c, (last + 1) / (double)NG_AUDIO_HZ, loops, peak,
                ng_notes.fm - n0.fm, ng_notes.ssg - n0.ssg, ng_notes.adpcm_a - n0.adpcm_a, ng_notes.adpcm_b - n0.adpcm_b);
        fflush(f);
    }
    free(base);
    fclose(f);
    return 0;
}

/* Audio energy over n frames of the sound menu (mean of |sample|). */
static double sounds_energy(int n)
{
    int16_t b[4096 * 2];
    double sum = 0;
    long count = 0;
    for (int i = 0; i < n; i++) {
        sounds_frame();
        uint32_t k;
        while ((k = ng_audio_read(b, 4096)) > 0) {
            for (uint32_t j = 0; j < 2 * k; j++) sum += b[j] < 0 ? -b[j] : b[j];
            count += (long)(2 * k);
        }
    }
    return count ? sum / count : 0;
}

static int test_sounds(const Options *o)
{
    ng_before_input_read = NULL;
    int16_t b[4096 * 2];
    while (ng_audio_read(b, 4096) > 0) {}
    size_t size;
    uint8_t *before = ng_state_save(&size);
    uint32_t a = fingerprint_span(o, 300);           /* the game without opening the menu */
    ng_state_load(before, size);

    sounds_show();
    double silence = sounds_energy(10);              /* after 03 03 06: the game goes quiet */
    sounds_navigate(1);                               /* Music: second entry (Mission 1) */
    sounds_navigate(4);
    double music = sounds_energy(300);
    sounds_navigate(3);                               /* Effects */
    sounds_navigate(3);                               /* Voices */
    sounds_navigate(4);
    double voice = sounds_energy(120);
    sounds_close();
    while (ng_audio_read(b, 4096) > 0) {}
    uint32_t d = fingerprint_span(o, 300);           /* the game after closing it */
    free(before);
    int ok = a == d && music > 200 && voice > 20 && silence < music / 10;
    printf("sound menu: silence %.0f, music %.0f, voice %.0f; game after %08X / without menu %08X: %s\n",
           silence, music, voice, d, a, ok ? "OK" : "FAIL");
    return ok ? 0 : 1;
}

/* Before ng_power_on: the WAV needs audio active from the first frame. */
int headless_prepare(const Options *o)
{
    if (o->wav) {
        wav = fopen(o->wav, "wb");
        if (!wav) { fprintf(stderr, "Could not create %s\n", o->wav); return 1; }
        wav_header(wav, 0);
        ng_audio_enabled = 1;
    }
    if (o->test_state || o->catalog || o->test_sounds) ng_audio_enabled = 1;
    return 0;
}

int headless_run(const Options *o)
{
    if (o->blood)
        ng_forced_read[NG_FORCED_BLOOD] = (NgForcedRead){1, PROFILE_BLOOD_WORD, PROFILE_BLOOD_MASK, 0x0001, 0, 0};
    /* Debug mode (cheats.h): PD_DIPS=hex forces debug dips, PD_DEVMODE the developer mode. */
    if (getenv("PD_DIPS")) cheats_dips = (uint16_t)strtoul(getenv("PD_DIPS"), NULL, 16);
    if (getenv("PD_DEVMODE")) cheats_devmode = 1;
    cheats_apply();
    if (o->script && script_load(o->script) != 0) return 2;
    if (o->inputs && inputs_load(o->inputs) != 0) return 2;
    instr_trace_init();
    if (getenv("PD_TRACE_READS")) {
        ng_trace_reads = fopen(getenv("PD_TRACE_READS"), "w");
        if (!ng_trace_io) { ng_trace_from = 0; ng_trace_to = INT64_MAX; }
    }
    FILE *fingerprints = o->fingerprints ? fopen(o->fingerprints, "w") : NULL;
    if (fingerprints) { fingerprint_file = fingerprints; ng_on_vblank_ack = fingerprint_vblank; }
    Uint64 t0 = SDL_GetTicksNS();
    for (long n = 0; n < o->frames; n++) {
        if (o->inputs) inputs_apply();
        else script_apply();
        cheats_test();
        cheats_input(&ng.joy[0], &ng.system[0]);
        ng_run_frame();
        cheats_frame();
        count_end_frame();
        dump_sprites();
        capture_sprites();
        if (fingerprints) fingerprint(fingerprints, ng.frames - 1);
        if (requested(o->dump, ng.frames - 1)) {
            char path[1200];
            snprintf(path, sizeof path, "%s/ram_port_%06llu.bin", o->output ? o->output : ".", ng.frames - 1);
            FILE *v = fopen(path, "wb");
            if (v) { fwrite(ng.ram, 1, sizeof ng.ram, v); fclose(v); }
        }
        if (wav) wav_flush();
        /* MAME numbering (screen:frame_number()): the image completed while running frame N
           (ng.frames goes from N to N + 1) is frame N. Verified with screenshots: 447 of 450
           consecutive frames identical with this numbering. */
        unsigned long long label = ng.frames - 1;
        if (requested(o->screenshots, label)) {
            char path[1200];
            snprintf(path, sizeof path, "%s/port_%06llu.bmp", o->output ? o->output : ".", label);
            if (save_bmp(path) != 0) fprintf(stderr, "Could not write %s\n", path);
        }
    }
    double s = (double)(SDL_GetTicksNS() - t0) / 1e9;
    printf("frames: %llu in %.2f s (%.0f frames/s)  recompiled steps: %llu  interpreted: %llu\n",
           (unsigned long long)ng.frames, s, ng.frames / (s > 0 ? s : 1),
           (unsigned long long)rc_recompiled_steps, (unsigned long long)rc_interpreted_steps);
    printf("unmapped reads: %llu  unmapped writes: %llu  watchdog resets: %u\n",
           (unsigned long long)ng.unmapped_reads, (unsigned long long)ng.unmapped_writes,
           ng.watchdog_resets);
    if (count_pc)
        printf("PC %06X: %llu times in %llu frames; frames without passing: %llu (%.1f%%)\n", count_pc, count_total,
               frames_counted, frames_without_loop, 100.0 * frames_without_loop / (frames_counted ? frames_counted : 1));
    if (wav) { wav_header(wav, wav_samples); fclose(wav); }
    if (fingerprints) fclose(fingerprints);
    int r = o->test_state ? test_state(o) : 0;
    if (o->catalog) r |= sound_catalog(o);
    if (o->test_sounds) r |= test_sounds(o);
    ng_roms_free(&ng_roms);
    return r;
}
