/* Headless mode: deterministic runs to verify against MAME (Phases 3 and 4).
 *
 * BMP screenshots, input script, inputs recorded in MAME (per frame or timed), fingerprints
 * per frame and per VBlank, per-instruction traces and WAV of the generated audio.
 *
 * PD_SPAWN_POKES=file: the RAM bytes each spawn cheat changed, as PD_POKE entries (cheats.h).
 *
 * PD_WATCH_W=hex: logs every write to that work RAM word (frame, PC, value, byte mask) to stdout;
 * with PD_INTERPRETER=1 the PC is the instruction's (research: who writes a variable).
 *
 * PD_POKE="frame,address,value[;...]" (hex address and value): writes a work RAM byte when that
 * frame ends, as tools/mame/scripted_run.lua does in MAME's frame notifier. Used for Metal Slug 3's
 * mission runs: the start mission (0x10ED7A) written during the soldier select, the path of the
 * game's own memory card continue (docs/PHASE4_PROFILE.md).
 *
 * Metal Slug's Boss Rush (docs/BOSS_RUSH.md, stage 3): PD_FFWD=mission,folder runs the
 * fast-forward through a mission and writes its bosses' states (research: PD_FFWD_KILL,
 * PD_FFWD_BOT); PD_BOSSRUSH with PD_BOSSRUSH_DIR keeps the states there (made first if missing);
 * PD_BOSSRUSH_KILL=n defeats each boss n frames after it loads (tests, with a built-in bot);
 * PD_BOSSRUSH_FROM=n starts at the n-th boss; PD_LOAD_STATE=file starts from a saved state.
 */
#include <SDL3/SDL.h>

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "dispatcher.h"
#include "neogeo.h"
#include "cheats.h"
#include "progress.h"
#include "records.h"
#include "bossbar.h"
#include "bossrush.h"
#include "profile.h"
#include "port.h"
#include "sounds.h"
#include "runahead.h"
#include "fastfwd.h"

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
                ng.ram[((player ? game_profile.player2_object : game_profile.player1_object) + (value >> 8)) & 0xFFFF] = (uint8_t)value;
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

/* ------------------------------------------------------------------ RAM writes (PD_POKE) */
typedef struct { unsigned long long frame; uint32_t addr; uint8_t value; } Poke;
static Poke pokes[2048];                /* Metal Slug: stage jumps, lives and bombs over long runs */
static int n_pokes = -1;

static void pokes_apply(void)
{
    if (n_pokes < 0) {
        n_pokes = 0;
        const char *e = getenv("PD_POKE");
        for (const char *q = e; q && *q && n_pokes < 2048;) {
            unsigned long long f;
            unsigned a, v;
            int used;
            if (sscanf(q, "%llu,%x,%x%n", &f, &a, &v, &used) != 3) break;
            pokes[n_pokes++] = (Poke){f, a, (uint8_t)v};
            q += used;
            if (*q == ';') q++;
        }
    }
    for (int i = 0; i < n_pokes; i++)
        if (pokes[i].frame + 1 == ng.frames && pokes[i].addr >= 0x100000 && pokes[i].addr < 0x110000)
            ng.ram[pokes[i].addr & 0xFFFF] = pokes[i].value;
}

/* PD_KILL_ENEMIES=maxlife[,from]: from that frame on, at every VBlank acknowledge (after its
   fingerprint, the instant tools/mame/scripted_run.lua uses too: a frame's end can fall in the
   middle of an object's creation, at different points in MAME and here), every object in the list
   but the players and the vehicle whose life word is 1..maxlife gets the game's own death mark
   (the damage routine's: life 0 and its dead bit). Metal Slug's fast-forward study; needs the
   profile's object fields. */
static void kill_enemies(void)
{
    static int max = -1;
    static unsigned long long from;
    if (max < 0) {
        max = 0;
        if (getenv("PD_KILL_ENEMIES")) sscanf(getenv("PD_KILL_ENEMIES"), "%d,%llu", &max, &from);
    }
    if (max && ng.frames >= from) ffwd_kill(max, 0, 0);
}

/* PD_FFWD=m,folder: Boss Rush's fast-forward (fastfwd.h) through mission m (1-based): each of the
   profile's bosses in it found is printed and its state written to folder/boss<n>.st (n: its
   place in the profile's Boss Rush, 1-based; PD_LOAD_STATE loads it). */
static int fast_forward(const char *spec)
{
    int m = 0, first = -1, count = 0;
    char dir[1000] = "";
    if (sscanf(spec, "%d,%999[^\n]", &m, dir) == 2)
        for (int i = 0; i < game_profile.n_rush_states; i++)
            if (game_profile.rush_states[i].mission == m - 1) { if (first < 0) first = i; count++; }
    if (first < 0) {
        fprintf(stderr, "PD_FFWD: m,folder (m: a mission with Boss Rush bosses)\n");
        return 2;
    }
    if (getenv("PD_FFWD_KILL")) ffwd_kill_max = atoi(getenv("PD_FFWD_KILL"));
    /* PD_FFWD_BOT=right,left,jump,grenade,seed (research): the mission's bots changed */
    static ProfileRushState bots[16];
    const ProfileRushState *from = &game_profile.rush_states[first];
    if (getenv("PD_FFWD_BOT") && count <= 16) {
        int v[5];
        if (sscanf(getenv("PD_FFWD_BOT"), "%d,%d,%d,%d,%d", &v[0], &v[1], &v[2], &v[3], &v[4]) == 5)
            for (int i = 0; i < count; i++) {
                bots[i] = from[i];
                bots[i].right = (uint8_t)v[0]; bots[i].left = (uint8_t)v[1];
                bots[i].jump = (uint8_t)v[2]; bots[i].grenade = (uint8_t)v[3]; bots[i].seed = (uint32_t)v[4];
            }
        from = bots;
    }
    ffwd_begin(from, count);
    uint64_t t0 = SDL_GetTicksNS();
    int st, found = 0;
    do {
        st = ffwd_run(100);
        for (; found < ffwd_found(); found++) {
            const ProfileRushState *r = &game_profile.rush_states[first + found];
            size_t size;
            FfCarry c;
            const uint8_t *d = ffwd_state(found, &size, &c);
            char path[1100];
            snprintf(path, sizeof path, "%s/boss%d.st", dir, first + found + 1);
            FILE *f = fopen(path, "wb");
            if (f) { fwrite(d, 1, size, f); fclose(f); }
            printf("ffwd %d (%s): boss at frame %llu, state %zu bytes; new game: lives %u bombs %u weapon %u\n",
                   first + found + 1, game_profile.bosses[r->boss].name ? game_profile.bosses[r->boss].name : "boss",
                   (unsigned long long)ng.frames, size, c.lives, c.bombs, c.weapon);
        }
    } while (st == FF_RUNNING);
    if (st != FF_DONE) printf("ffwd %d: failed at frame %llu\n", first + found + 1, (unsigned long long)ng.frames);
    printf("ffwd: %.1f s\n", (double)(SDL_GetTicksNS() - t0) / 1e9);
    ffwd_end();
    return st == FF_DONE ? 0 : 1;
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
                const char *dir = getenv("PD_DUMP_DIR") ? getenv("PD_DUMP_DIR") : ".";
                snprintf(path, sizeof path, "%s/ram_port_v%06llu.bin", dir, v);
                FILE *f = fopen(path, "wb");
                if (f) { fwrite(ng.ram, 1, sizeof ng.ram, f); fclose(f); }
                /* PD_DUMP_VRAM: also the LSPC VRAM (big-endian words), as fingerprints.lua does */
                snprintf(path, sizeof path, "%s/vram_port_v%06llu.bin", dir, v);
                if (getenv("PD_DUMP_VRAM") && (f = fopen(path, "wb"))) {
                    for (size_t i = 0; i < sizeof ng.vram / 2; i++) { fputc(ng.vram[i] >> 8, f); fputc(ng.vram[i] & 0xFF, f); }
                    fclose(f);
                }
            }
            p = *end == ',' ? end + 1 : end;
        }
    }
}

/* PD_TRACE_LIFE=min (research: finding bosses): at every VBlank acknowledge, each object of the
   profile's list whose life (1..0x6FFF) reaches min is printed once per object and routine
   ("life f=frame m=mission obj=address rt=routine life=life parent=address"). */
static void trace_life(void)
{
    static int min = -1, n;
    static uint32_t seen[512][2];
    if (min < 0) min = getenv("PD_TRACE_LIFE") ? atoi(getenv("PD_TRACE_LIFE")) : 0;
    if (!min || !game_profile.object_list) return;
    uint32_t head = game_profile.object_list, a = head;
    for (int k = 0; k < 400; k++) {
        a = (uint32_t)ng.ram[(a + 8) & 0xFFFF] << 24 | (uint32_t)ng.ram[(a + 9) & 0xFFFF] << 16 |
            (uint32_t)ng.ram[(a + 10) & 0xFFFF] << 8 | ng.ram[(a + 11) & 0xFFFF];
        if (a == head || a < 0x100000 || a >= 0x10F000) break;
        uint32_t rt = (uint32_t)ng.ram[a & 0xFFFF] << 24 | (uint32_t)ng.ram[(a + 1) & 0xFFFF] << 16 |
                      (uint32_t)ng.ram[(a + 2) & 0xFFFF] << 8 | ng.ram[(a + 3) & 0xFFFF];
        int life = ng.ram[(a + game_profile.object_life) & 0xFFFF] << 8 | ng.ram[(a + game_profile.object_life + 1) & 0xFFFF];
        if (life < min || life >= 0x7000) continue;
        int known = 0;
        for (int i = 0; i < n && !known; i++) known = seen[i][0] == a && seen[i][1] == rt;
        if (known) continue;
        if (n < 512) { seen[n][0] = a; seen[n][1] = rt; n++; }
        uint32_t parent = (uint32_t)ng.ram[(a + 12) & 0xFFFF] << 24 | (uint32_t)ng.ram[(a + 13) & 0xFFFF] << 16 |
                          (uint32_t)ng.ram[(a + 14) & 0xFFFF] << 8 | ng.ram[(a + 15) & 0xFFFF];
        printf("life f=%llu m=%d obj=%06X rt=%06X life=%d parent=%06X\n", (unsigned long long)ng.frames,
               game_profile.mission_now ? ng.ram[game_profile.mission_now & 0xFFFF] + 1 : 0, (unsigned)a,
               (unsigned)rt, life, (unsigned)parent);
    }
}

/* PD_KILL_FLAG=address,from[,until] (hex address): from that frame on (and before `until`), 0xFF
   written to that byte at every VBlank acknowledge, as tools/mame/scripted_run.lua does. Metal
   Slug X: the game's kill-everything request 0x1067E3 (docs/BOSS_RUSH.md), for boss runs against
   MAME. */
static void kill_flag(void)
{
    static int init;
    static unsigned addr;
    static unsigned long long from, until = ~0ull;
    if (!init) {
        init = 1;
        if (!getenv("PD_KILL_FLAG") || sscanf(getenv("PD_KILL_FLAG"), "%x,%llu,%llu", &addr, &from, &until) < 2) addr = 0;
    }
    if (addr && ng.frames >= from && ng.frames < until) ng.ram[addr & 0xFFFF] = 0xFF;
}

static void vblank_hook(void)
{
    if (fingerprint_file) fingerprint_vblank();
    kill_enemies();
    kill_flag();
    trace_life();
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

/* PD_WATCH_PC=hex[,first_frame]: logs every execution of that address (frame, cycle, return address,
   D0-D7, A0-A6) to stdout, all through the interpreter (research). */
static uint32_t watch_pc;
static unsigned long long watch_from;

static void watch_instr(const Cpu68k *c)
{
    if (c->pc != watch_pc || ng.frames < watch_from) return;
    uint32_t sp = c->a[7];
    printf("W f=%llu c=%lld ret=%06X", (unsigned long long)ng.frames, (long long)c->cycles, bus_r32(sp));
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

/* Audio energy over n frames of the sound menu: the mean of |sample - the samples' mean| (Metal
   Slug 5's mixer leaves a constant level, which is not sound). */
static double sounds_energy(int n)
{
    int16_t b[4096 * 2];
    int16_t *all = NULL;
    double sum = 0;
    long count = 0, cap = 0;
    for (int i = 0; i < n; i++) {
        sounds_frame();
        uint32_t k;
        while ((k = ng_audio_read(b, 4096)) > 0) {
            if (count + 2 * (long)k > cap) {
                cap = (count + 2 * (long)k) * 2;
                int16_t *p = realloc(all, (size_t)cap * sizeof *all);
                if (!p) { free(all); return 0; }
                all = p;
            }
            for (uint32_t j = 0; j < 2 * k; j++) { sum += b[j]; all[count++] = b[j]; }
        }
    }
    double mean = count ? sum / count : 0, dev = 0;
    for (long j = 0; j < count; j++) dev += fabs(all[j] - mean);
    free(all);
    return count ? dev / count : 0;
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
    /* The silence commands go one per frame: the game goes quiet once they are sent (Metal Slug
       4's driver only stops the music when its whole sequence has arrived). */
    sounds_energy(game_profile.n_sound_stop ? game_profile.n_sound_stop : 3);
    double silence = sounds_energy(10);
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
    if (o->blood || getenv("PD_BLOOD"))           /* PD_BLOOD: the MAME runs' variable (mame_run.py) */
        ng_forced_read[NG_FORCED_BLOOD] = (NgForcedRead){1, game_profile.blood_word, game_profile.blood_mask,
                                                         (uint16_t)(game_profile.blood_mask & 0x0101), 0, 0};
    /* Debug mode (cheats.h): PD_DIPS=hex forces debug dips, PD_DEVMODE the developer mode. */
    if (getenv("PD_DIPS")) cheats_dips = (uint16_t)strtoul(getenv("PD_DIPS"), NULL, 16);
    if (getenv("PD_DEVMODE")) cheats_devmode = 1;
    if (getenv("PD_START_MISSION")) cheats_start_mission = atoi(getenv("PD_START_MISSION")) - 1;   /* 1 = normal */
    /* PD_MISSION_ONCE=n: Mission mode's start (window.c fe_mission_start), mission n (1 = first);
       with the PLAY MANUAL known, its direct start from the end of the boot (the script brings
       the coin and START) */
    if (getenv("PD_MISSION_ONCE")) {
        cheats_start_mission_once = atoi(getenv("PD_MISSION_ONCE")) - 1;
        if (game_profile.manual_word) cheats_mission_from = game_profile.boot_frames;
        /* PD_SECTION_ONCE=s (research): with it, at section s (the Boss Rush fast-forward's start) */
        if (getenv("PD_SECTION_ONCE")) cheats_start_section_once = atoi(getenv("PD_SECTION_ONCE"));
    }
    /* PD_BOSSRUSH=1: Boss Rush from the first new game (bossrush.c); its steps are printed. Saved
       states (Metal Slug): in PD_BOSSRUSH_DIR (default: PD_PREF's bossrush folder), made first if
       missing. */
    if (getenv("PD_BOSSRUSH")) {
        char d[1100];
        if (getenv("PD_BOSSRUSH_DIR")) snprintf(d, sizeof d, "%s", getenv("PD_BOSSRUSH_DIR"));
        else snprintf(d, sizeof d, "%sbossrush/", getenv("PD_PREF") ? getenv("PD_PREF") : "");
        bossrush_set_dir(d);
        if (bossrush_needs_prepare()) {
            Uint64 t = SDL_GetTicksNS();
            int r, shown = -1;
            bossrush_prepare_begin();
            while ((r = bossrush_prepare_step(100)) == 0)
                if (bossrush_prepare_boss() != shown) {
                    shown = bossrush_prepare_boss();
                    printf("bossrush: preparing boss %d/%d\n", shown + 1, bossrush_total());
                }
            printf("bossrush: %s in %.1f s\n", r > 0 ? "prepared" : "preparation failed", (double)(SDL_GetTicksNS() - t) / 1e9);
            if (r < 0) return 1;
        }
        if (getenv("PD_BOSSRUSH_FROM")) bossrush_first = atoi(getenv("PD_BOSSRUSH_FROM")) - 1;   /* tests: 1-based */
        if (!bossrush_start()) { fprintf(stderr, "PD_BOSSRUSH: not started\n"); return 1; }
    }
    cheats_apply();
    if (getenv("PD_FFWD")) return fast_forward(getenv("PD_FFWD"));
    /* PD_LOAD_STATE=file: start from a saved state (PD_SAVE_STATE, PD_FFWD) instead of power-on. */
    if (getenv("PD_LOAD_STATE")) {
        size_t size;
        void *d = SDL_LoadFile(getenv("PD_LOAD_STATE"), &size);
        int bad = !d || ng_state_load(d, size) != 0;
        SDL_free(d);
        if (bad) { fprintf(stderr, "PD_LOAD_STATE: %s is not a state of this version\n", getenv("PD_LOAD_STATE")); return 2; }
    }
    if (o->script && script_load(o->script) != 0) return 2;
    if (o->inputs && inputs_load(o->inputs) != 0) return 2;
    instr_trace_init();
    /* PD_PROGRESS=file: mission mode progress kept there (progress.c), unlocks printed. */
    const char *progress_file = getenv("PD_PROGRESS");
    if (progress_file) progress_load(progress_file);
    /* PD_RECORDS=file: records kept there (records.c), completed missions printed. */
    const char *records_file = getenv("PD_RECORDS");
    if (records_file) records_load(records_file);
    if (getenv("PD_SPAWN_POKES")) cheats_spawn_log = fopen(getenv("PD_SPAWN_POKES"), "w");
    if (getenv("PD_WATCH_W")) ng_watch_write = (uint32_t)strtoul(getenv("PD_WATCH_W"), NULL, 16) & 0xFFFFFE;
    if (getenv("PD_TRACE_READS")) {
        ng_trace_reads = fopen(getenv("PD_TRACE_READS"), "w");
        if (!ng_trace_io) { ng_trace_from = 0; ng_trace_to = INT64_MAX; }
    }
    FILE *fingerprints = o->fingerprints ? fopen(o->fingerprints, "w") : NULL;
    if (fingerprints) fingerprint_file = fingerprints;
    if (fingerprints || getenv("PD_KILL_ENEMIES") || getenv("PD_KILL_FLAG") || getenv("PD_TRACE_LIFE")) ng_on_vblank_ack = vblank_hook;
    Uint64 t0 = SDL_GetTicksNS();
    for (long n = 0; n < o->frames; n++) {
        if (o->inputs) inputs_apply();
        else script_apply();
        /* PD_BOSSRUSH_KILL (tests): without a script, player 1 plays a plain bot (mostly right,
           firing, jumping) that reaches each boss and walks out of each mission */
        if (!o->script && !o->inputs && getenv("PD_BOSSRUSH_KILL")) {
            uint64_t k = ng.frames % 240;
            ng.joy[0] = (uint8_t)((k < 200 ? NG_RIGHT : k >= 215 && k <= 235 ? NG_LEFT : 0) | (k % 6 <= 2 ? NG_A : 0) |
                                  ((k >= 40 && k <= 47) || (k >= 130 && k <= 137) ? NG_B : 0));
        }
        cheats_test();
        cheats_input(&ng.joy[0], &ng.system[0]);
        ng_run_frame();
        pokes_apply();
        int once = cheats_start_mission_once;
        cheats_frame();
        if (once >= 0 && cheats_start_mission_once < 0)       /* PD_MISSION_ONCE: the mission written */
            printf("start mission %d written at frame %llu\n", once + 1, (unsigned long long)ng.frames - 1);
        if (progress_file) {
            int m = progress_frame();
            if (m) printf("mission %d unlocked at frame %llu\n", m, (unsigned long long)ng.frames - 1);
            if (progress_take_completed()) printf("game completed at frame %llu\n", (unsigned long long)ng.frames - 1);
        }
        /* PD_SAVE_STATE=frame,file: the machine's state when that frame ends (window checks load
           it with PD_LOAD_STATE). */
        if (getenv("PD_SAVE_STATE")) {
            unsigned long long at;
            char path[1024];
            if (sscanf(getenv("PD_SAVE_STATE"), "%llu,%1023[^\n]", &at, path) == 2 && ng.frames - 1 == at) {
                size_t size;
                uint8_t *st = ng_state_save(&size);
                FILE *sf = st ? fopen(path, "wb") : NULL;
                if (sf) { fwrite(st, 1, size, sf); fclose(sf); }
                free(st);
            }
        }
        if (getenv("PD_BOSSRUSH")) {
            /* PD_BOSSRUSH_KILL=n (saved states, tests): each boss dies n frames after the last
               step, and player 1 keeps 9 lives */
            static uint64_t since;
            if (getenv("PD_BOSSRUSH_KILL") && bossrush_active()) {
                if (++since >= (uint64_t)atoi(getenv("PD_BOSSRUSH_KILL"))) bossrush_test_defeat();
                if (since % 300 == 0) cheats_set_lives(0, 9);
            }
            int r = bossrush_frame();
            if (r) since = 0;
            if (r) printf("bossrush %llu: %s, %d/%d bosses defeated\n", (unsigned long long)ng.frames - 1,
                          r == BR_CLEARED ? "cleared" : "next boss", bossrush_defeated(), bossrush_total());
            if (r == BR_NEXT && game_profile.n_rush_states) {          /* saved states: the values carried */
                FfCarry c;
                ffwd_carry_read(&c);
                printf("bossrush: mission %d, score %02X%02X%02X%02X, lives %u, bombs %u, weapon %u\n",
                       ng.ram[game_profile.mission_now & 0xFFFF] + 1, c.score[0], c.score[1], c.score[2], c.score[3],
                       c.lives, c.bombs, c.weapon);
            }
        }
        /* PD_BOSSBAR=1: the health bar's changes (bossbar.c), printed with their frame. */
        if (getenv("PD_BOSSBAR")) {
            static char last[96];
            bossbar_frame();
            const char *s = bossbar_describe();
            if (strcmp(s, last)) {
                printf("bossbar %llu: %s\n", (unsigned long long)ng.frames - 1, s[0] ? s : "hidden");
                snprintf(last, sizeof last, "%s", s);
            }
        }
        RecordEvent rec;
        if (records_file && records_frame(&rec)) {
            char t[32];
            records_time_text(rec.frames, t, sizeof t);
            printf("mission %d completed at frame %llu: %u frames (%s), score %u%s%s%s\n", rec.mission,
                   (unsigned long long)ng.frames - 1, (unsigned)rec.frames, t, (unsigned)rec.score,
                   rec.clean ? "" : rec.turbo ? " (not clean: slowdown off)" : " (not clean)", rec.new_time ? ", new best time" : "",
                   rec.new_score ? ", new best score" : "");
        }
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
        /* PD_RUNAHEAD=n: run-ahead after every frame, as the window does (the real frames must not
           change: mslug3_start_runahead_vs_mame) */
        if (getenv("PD_RUNAHEAD")) runahead_frames(atoi(getenv("PD_RUNAHEAD")));
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
