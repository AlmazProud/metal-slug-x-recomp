/* Boss Rush (bossrush.h).
 *
 * Sections (Metal Slug X, 3, 4 and 5). The start bytes (game_profile.start_mission: mission,
 * section) are watched through the RAM write hook (ng_hook_word). The section byte is 0xFF while
 * a section runs; the game writes the next section there when one ends (the next mission's first
 * section, or the mission's next one) and the stage start routine reads it right after, in the
 * same frame. So:
 *   - the first boss: the new game's start bytes are set like Mission mode's (cheats.c); its
 *     section runs when the game writes 0xFF over that boss's section;
 *   - when a boss section ends (any other value written over 0xFF), the next boss's mission and
 *     section are written at once in its place: the stage start loads it, with the score,
 *     lives, bombs and weapon carried over as between two missions;
 *   - after the last boss nothing is changed: the game's own ending follows.
 *
 * Saved states (Metal Slug: no sections; game_profile.rush_states). Each boss starts from a state
 * the fast-forward (fastfwd.h) saved shortly before it appears, kept in the preferences folder
 * (bossrush_set_dir) and made the first time ("preparing"). During the run the backup RAM is the
 * states' (the BIOS's defaults); the player's comes back when it stops. The first boss starts with
 * a new game's values;
 * each next one with the score, lives, bombs, weapon and ammo the player had. A boss is behind
 * once its mission goes on to the next one (a mission's last boss: the game plays its "mission
 * complete" first), or 90 frames after a middle boss died; after the last one the game's own
 * ending follows.
 */
#include "bossrush.h"

#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bossbar.h"
#include "cheats.h"
#include "fastfwd.h"
#include "neogeo.h"
#include "profile.h"

enum { WAIT_FIRST, RUNNING, LOADING };
static int active, seg, defeated, phase, event;
int bossrush_first;

static int states_mode(void) { return game_profile.n_rush == 0 && game_profile.n_rush_states > 0; }

int bossrush_supported(void)
{
    if (states_mode()) return game_profile.object_list && game_profile.mission_now && profile_supported();
    return game_profile.n_rush > 0 && game_profile.start_mission && profile_supported();
}
int bossrush_active(void) { return active; }
int bossrush_defeated(void) { return defeated; }

int bossrush_total(void)
{
    if (states_mode()) return game_profile.n_rush_states;
    int n = 0;
    for (int i = 0; i < game_profile.n_rush; i++) n += game_profile.rush[i].bosses;
    return n;
}

static void on_write(uint16_t before, uint16_t after)
{
    if (!active) return;
    int old_section = before & 0xFF, section = after & 0xFF;
    const ProfileRush *r = &game_profile.rush[seg];
    switch (phase) {
    case WAIT_FIRST:
        /* the first boss section runs: 0xFF written over its section (the soldier select writes
           the bytes too, with other values) */
        if (section == 0xFF && old_section == r->section && (after >> 8) == r->mission) phase = RUNNING;
        break;
    case LOADING:
        /* the next boss section runs: 0xFF written once it is loaded (within a mission the game
           may write its own next section again after ours was read) */
        if (section == 0xFF && (after >> 8) == r->mission) phase = RUNNING;
        break;
    case RUNNING:
        if (old_section != 0xFF || section == 0xFF) break;
        /* the boss section ended: the game asks for another section */
        defeated += r->bosses;
        if (seg + 1 >= game_profile.n_rush) {
            event = BR_CLEARED;
            active = 0;
            ng_hook_word = 0;
            break;
        }
        seg++;
        uint8_t *b = ng.ram + (game_profile.start_mission & 0xFFFF);
        b[0] = game_profile.rush[seg].mission;
        b[1] = game_profile.rush[seg].section;
        phase = LOADING;
        event = BR_NEXT;
        break;
    }
}

/* ------------------------------------------------------------------ saved states: the files */
#define MAGIC "PDRUSH2"
typedef struct {
    char magic[8];
    char program[65];                    /* game_profile.windows_sha256 */
    uint8_t carry[sizeof(FfCarry)];      /* a new game's values */
    uint32_t size;                       /* the state's */
} Header;

static char dir[1024];
static uint8_t *loaded[16];              /* the run's states, read at its start */
static size_t loaded_size[16];
static FfCarry new_game;

void bossrush_set_dir(const char *d) { snprintf(dir, sizeof dir, "%s", d ? d : ""); }

static void path_of(int i, char *p, size_t n) { snprintf(p, n, "%sboss%d.state", dir, i + 1); }

static size_t state_size(void)
{
    static size_t size;
    if (!size) {
        uint8_t *s = ng_state_save(&size);
        free(s);
    }
    return size;
}

/* State i's file, checked (this program, this emulator's state size); NULL if missing or old. */
static uint8_t *read_state(int i, size_t *size, FfCarry *carry)
{
    char p[1100];
    path_of(i, p, sizeof p);
    FILE *f = fopen(p, "rb");
    if (!f) return NULL;
    Header h;
    uint8_t *d = NULL;
    if (fread(&h, sizeof h, 1, f) == 1 && !memcmp(h.magic, MAGIC, sizeof MAGIC) &&
        !strncmp(h.program, game_profile.windows_sha256 ? game_profile.windows_sha256 : "", sizeof h.program) &&
        h.size == state_size() && (d = malloc(h.size)) && fread(d, 1, h.size, f) == h.size) {
        *size = h.size;
        if (carry) memcpy(carry, h.carry, sizeof *carry);
    } else {
        free(d);
        d = NULL;
    }
    fclose(f);
    return d;
}

static int write_state(int i, const uint8_t *d, size_t size, const FfCarry *carry)
{
    char p[1100];
    snprintf(p, sizeof p, "%s", dir);
    SDL_CreateDirectory(p);
    path_of(i, p, sizeof p);
    Header h;
    memset(&h, 0, sizeof h);
    memcpy(h.magic, MAGIC, sizeof MAGIC);
    snprintf(h.program, sizeof h.program, "%s", game_profile.windows_sha256 ? game_profile.windows_sha256 : "");
    memcpy(h.carry, carry, sizeof *carry);
    h.size = (uint32_t)size;
    FILE *f = fopen(p, "wb");
    if (!f) return 0;
    int ok = fwrite(&h, sizeof h, 1, f) == 1 && fwrite(d, 1, size, f) == size;
    return fclose(f) == 0 && ok;
}

static int have_state(int i)
{
    size_t size;
    uint8_t *d = read_state(i, &size, NULL);
    free(d);
    return d != NULL;
}

int bossrush_needs_prepare(void)
{
    if (!states_mode()) return 0;
    for (int i = 0; i < game_profile.n_rush_states; i++)
        if (!have_state(i)) return 1;
    return 0;
}

/* ------------------------------------------------------------------ saved states: preparing */
static int prep_group = -1, prep_count, prep_written;
static uint8_t prep_sram[sizeof ng.sram];  /* the player's backup RAM: the fast-forward's is the factory's */
static int have_prep_sram;

static void prep_done(void)
{
    if (have_prep_sram) memcpy(ng.sram, prep_sram, sizeof ng.sram);
    have_prep_sram = 0;
    prep_group = -1;
}

/* The next mission with a state missing: its bosses from prep_group (prep_count of them). */
static int next_group(int from)
{
    for (int i = from; i < game_profile.n_rush_states;) {
        int n = 1;
        while (i + n < game_profile.n_rush_states && game_profile.rush_states[i + n].mission == game_profile.rush_states[i].mission) n++;
        for (int k = 0; k < n; k++)
            if (!have_state(i + k)) {
                prep_group = i;
                prep_count = n;
                prep_written = 0;
                ffwd_begin(&game_profile.rush_states[i], n);
                return 1;
            }
        i += n;
    }
    prep_group = -1;
    return 0;
}

void bossrush_prepare_begin(void)
{
    memcpy(prep_sram, ng.sram, sizeof prep_sram);
    have_prep_sram = 1;
    if (!next_group(0)) prep_done();
}

int bossrush_prepare_step(int ms)
{
    if (prep_group < 0) return 1;
    uint64_t until = SDL_GetTicksNS() + (uint64_t)ms * 1000000;
    while (SDL_GetTicksNS() < until) {
        int st = ffwd_run(20);
        /* each state is written as soon as it is found: a cancelled preparation keeps them */
        for (; prep_written < ffwd_found(); prep_written++) {
            size_t size;
            FfCarry c;
            const uint8_t *d = ffwd_state(prep_written, &size, &c);
            if (!d || !write_state(prep_group + prep_written, d, size, &c)) { ffwd_end(); prep_done(); return -1; }
        }
        if (st == FF_FAILED) { ffwd_end(); prep_done(); return -1; }
        if (st == FF_DONE) {
            ffwd_end();
            if (!next_group(prep_group + prep_count)) { prep_done(); return 1; }
        }
    }
    return 0;
}

void bossrush_prepare_cancel(void)
{
    if (prep_group >= 0) ffwd_end();
    prep_done();
}

/* The boss being prepared (0-based, its place in Boss Rush) and the whole preparation's share
   done (0..1, by bosses). */
int bossrush_prepare_boss(void) { return prep_group < 0 ? 0 : prep_group + ffwd_found(); }

float bossrush_prepare_progress(void)
{
    if (prep_group < 0) return 1;
    float done = (float)prep_group + ffwd_progress() * (float)prep_count;
    return done / (float)game_profile.n_rush_states;
}

/* ------------------------------------------------------------------ saved states: the run */
static int seen, gone;
static FfCarry last;                     /* the player's values, while in the game */

/* A boss object in the list (any life), and alive (life above 0, no dead bit): 0, 1 or 2. */
static int boss_alive(const ProfileRushState *r)
{
    const ProfileBoss *b = &game_profile.bosses[r->boss];
    uint32_t head = game_profile.object_list, a = head;
    uint8_t mask = game_profile.object_dead_mask ? game_profile.object_dead_mask : 0x80;
    int found = 0;
    for (int n = 0; n < 400; n++) {
        a = (uint32_t)ng.ram[(a + 8) & 0xFFFF] << 24 | (uint32_t)ng.ram[(a + 9) & 0xFFFF] << 16 |
            (uint32_t)ng.ram[(a + 10) & 0xFFFF] << 8 | ng.ram[(a + 11) & 0xFFFF];
        if (a == head || a < 0x100000 || a >= 0x10F000) break;
        uint32_t code = (uint32_t)ng.ram[a & 0xFFFF] << 24 | (uint32_t)ng.ram[(a + 1) & 0xFFFF] << 16 |
                        (uint32_t)ng.ram[(a + 2) & 0xFFFF] << 8 | ng.ram[(a + 3) & 0xFFFF];
        int life = (int16_t)(ng.ram[(a + game_profile.object_life) & 0xFFFF] << 8 | ng.ram[(a + game_profile.object_life + 1) & 0xFFFF]);
        if (code < b->code_lo || code > b->code_hi) continue;
        if (life > 0 && life < 0x7000 && !(ng.ram[(a + game_profile.object_dead) & 0xFFFF] & mask)) return 2;
        found = 1;
    }
    return found;
}

/* The player's backup RAM (settings, high scores): kept from the run's start and given back when
   it stops. During the run it is the states' own, the BIOS's defaults: changing it under a game
   in progress makes the BIOS reset the machine. */
static uint8_t user_sram[sizeof ng.sram];
static int have_user_sram;

static int ammo_pending, lives_pending;
static uint8_t ammo[2];

/* Boss i's state, then the values carried: the weapon through the game's own routine (the
   soldier's sprite and the HUD follow it), its ammo once that is done. */
static int load_boss(int i, const FfCarry *carry)
{
    if (!loaded[i]) return 0;
    if (ng_state_load(loaded[i], loaded_size[i]) != 0) return 0;
    ffwd_carry_write(carry);
    lives_pending = game_profile.hud_lives_routine != 0;
    if (cheats_give_weapon(0, carry->weapon)) {
        ammo_pending = 1;
        memcpy(ammo, carry->ammo, sizeof ammo);
    }
    last = *carry;
    bossbar_reset();
    seen = gone = 0;
    return 1;
}

static void free_loaded(void)
{
    for (int i = 0; i < 16; i++) { free(loaded[i]); loaded[i] = NULL; }
}

static int start_states(void)
{
    free_loaded();
    for (int i = 0; i < game_profile.n_rush_states && i < 16; i++)
        if (!(loaded[i] = read_state(i, &loaded_size[i], i == 0 ? &new_game : NULL))) { free_loaded(); return 0; }
    seg = bossrush_first > 0 && bossrush_first < game_profile.n_rush_states ? bossrush_first : 0;
    defeated = 0;
    event = BR_NONE;
    memcpy(user_sram, ng.sram, sizeof user_sram);
    have_user_sram = 1;
    active = load_boss(seg, &new_game);
    phase = RUNNING;
    return active;
}

static void frame_states(void)
{
    const ProfileRushState *r = &game_profile.rush_states[seg];
    if (ammo_pending && !cheats_give_pending()) {
        ammo_pending = 0;
        ng.ram[(game_profile.player_table + game_profile.table_ammo) & 0xFFFF] = ammo[0];
        ng.ram[(game_profile.player_table + game_profile.table_ammo + 1) & 0xFFFF] = ammo[1];
    }
    if (lives_pending && ng_cpu.pc >= game_profile.wait_start && ng_cpu.pc < game_profile.wait_end) {
        uint32_t d[8] = {0}, a[8] = {0};       /* the HUD shows the lives carried */
        a[5] = 0x108000;
        a[6] = game_profile.hud_object_p1;
        a[7] = (ng_cpu.a[7] & ~1u) - 0x100;
        ng_call68k(game_profile.hud_lives_routine, d, a, 200000);
        lives_pending = 0;
    }
    /* read while the player is in the mission: when it ends the game clears them */
    if (cheats_in_game(0) && ng.ram[game_profile.mission_now & 0xFFFF] == r->mission) ffwd_carry_read(&last);
    int alive = boss_alive(r);
    if (alive) seen = 1;
    int over;
    /* a mission's last boss: the game loads the next mission (only after the boss; a game over
       clears the RAM instead) */
    if (r->final) over = ng.ram[game_profile.mission_now & 0xFFFF] == r->mission + 1;
    else over = seen && alive < 2 && ++gone >= 90;
    if (!over) return;
    defeated++;
    if (seg + 1 >= game_profile.n_rush_states) {          /* the last boss: the ending follows */
        event = BR_CLEARED;
        active = 0;
        free_loaded();
        return;
    }
    seg++;
    if (!load_boss(seg, &last)) { active = 0; free_loaded(); return; }
    event = BR_NEXT;
}

/* Tests: the current boss's objects take their whole life of damage through the game's damage
   routine (as a shot does: its death follows), while the game waits for the VBlank; with no
   routine in the profile they get its death mark instead. */
void bossrush_test_defeat(void)
{
    if (!active || !states_mode()) return;
    const ProfileBoss *b = &game_profile.bosses[game_profile.rush_states[seg].boss];
    if (!game_profile.damage_routine) {
        /* Metal Slug X: the game's kill request too (its bosses' guns die by it, not by the mark) */
        if (game_profile.kill_flag) ffwd_kill(0, 0, 0);
        ffwd_kill_range(b->code_lo, b->code_hi);
        return;
    }
    if (ng_cpu.pc < game_profile.wait_start || ng_cpu.pc >= game_profile.wait_end) return;
    uint32_t head = game_profile.object_list, a = head;
    for (int n = 0; n < 400; n++) {
        a = (uint32_t)ng.ram[(a + 8) & 0xFFFF] << 24 | (uint32_t)ng.ram[(a + 9) & 0xFFFF] << 16 |
            (uint32_t)ng.ram[(a + 10) & 0xFFFF] << 8 | ng.ram[(a + 11) & 0xFFFF];
        if (a == head || a < 0x100000 || a >= 0x10F000) break;
        uint32_t code = (uint32_t)ng.ram[a & 0xFFFF] << 24 | (uint32_t)ng.ram[(a + 1) & 0xFFFF] << 16 |
                        (uint32_t)ng.ram[(a + 2) & 0xFFFF] << 8 | ng.ram[(a + 3) & 0xFFFF];
        int life = (int16_t)(ng.ram[(a + game_profile.object_life) & 0xFFFF] << 8 | ng.ram[(a + game_profile.object_life + 1) & 0xFFFF]);
        if (code < b->code_lo || code > b->code_hi || life < 1 || life >= 0x7000) continue;
        /* the attacker: a scratch block under the stack whose +0x68 is negative (no score) */
        uint32_t sp = ng_cpu.a[7] & ~1u, scratch = sp - 0x200;
        uint8_t keep = ng.ram[(scratch + 0x68) & 0xFFFF];
        ng.ram[(scratch + 0x68) & 0xFFFF] = 0xFF;
        uint32_t d[8] = {0}, r[8] = {0};
        d[0] = (uint32_t)life;
        r[1] = a;
        r[5] = 0x108000;
        r[6] = scratch;
        r[7] = sp - 0x300;
        ng_call68k(game_profile.damage_routine, d, r, 200000);
        ng.ram[(scratch + 0x68) & 0xFFFF] = keep;
    }
}

/* ------------------------------------------------------------------ both */
int bossrush_start(void)
{
    if (!bossrush_supported()) return 0;
    if (states_mode()) return start_states();
    active = 1;
    seg = defeated = 0;
    phase = WAIT_FIRST;
    event = BR_NONE;
    ng_hook_word = game_profile.start_mission & 0xFFFFFE;
    ng_on_hook_write = on_write;
    cheats_start_mission_once = game_profile.rush[0].mission;
    cheats_start_section_once = game_profile.rush[0].section;
    return 1;
}

int bossrush_running(void) { return active && phase == RUNNING; }

void bossrush_stop(void)
{
    active = 0;
    if (states_mode()) {
        free_loaded();
        if (have_user_sram) memcpy(ng.sram, user_sram, sizeof ng.sram);
        have_user_sram = 0;
        return;
    }
    ng_hook_word = 0;
    cheats_start_section_once = -1;
}

int bossrush_frame(void)
{
    if (active && states_mode()) frame_states();
    int e = event;
    event = BR_NONE;
    return e;
}
