/* Fast-forward to a boss (fastfwd.h). */
#include "fastfwd.h"

#include <stdlib.h>
#include <string.h>

#include "cheats.h"
#include "neogeo.h"

#define RING 5                          /* states kept, one every RING_EVERY frames */
#define RING_EVERY 30
#define BEFORE 120                      /* the saved state is at least this many frames old */
#define SEGMENT 40                      /* the bot's segments */
#define REFILL 600                      /* lives, bombs and the weapon given again */

static uint32_t l32(uint32_t a)
{
    return (uint32_t)ng.ram[a & 0xFFFF] << 24 | (uint32_t)ng.ram[(a + 1) & 0xFFFF] << 16 |
           (uint32_t)ng.ram[(a + 2) & 0xFFFF] << 8 | ng.ram[(a + 3) & 0xFFFF];
}

static int life_of(uint32_t a)
{
    return (int16_t)(ng.ram[(a + game_profile.object_life) & 0xFFFF] << 8 | ng.ram[(a + game_profile.object_life + 1) & 0xFFFF]);
}

static uint8_t dead_mask(void) { return game_profile.object_dead_mask ? game_profile.object_dead_mask : 0x80; }

void ffwd_kill(int max_life, uint32_t keep_lo, uint32_t keep_hi)
{
    if (!game_profile.object_list) return;
    uint32_t head = game_profile.object_list, a = head;
    for (int n = 0; n < 400; n++) {
        a = l32(a + 8);
        if (a == head || a < 0x100000 || a >= 0x10F000) break;
        if (a == game_profile.player1_object || a == game_profile.player2_object || a == game_profile.vehicle_object)
            continue;
        uint32_t code = l32(a);
        if (keep_hi && code >= keep_lo && code <= keep_hi) continue;
        int v = life_of(a);
        if (v < 1 || v > max_life) continue;
        ng.ram[(a + game_profile.object_life) & 0xFFFF] = 0;
        ng.ram[(a + game_profile.object_life + 1) & 0xFFFF] = 0;
        ng.ram[(a + game_profile.object_dead) & 0xFFFF] |= dead_mask();
    }
}

void ffwd_kill_range(uint32_t lo, uint32_t hi)
{
    if (!game_profile.object_list) return;
    uint32_t head = game_profile.object_list, a = head;
    for (int n = 0; n < 400; n++) {
        a = l32(a + 8);
        if (a == head || a < 0x100000 || a >= 0x10F000) break;
        uint32_t code = l32(a);
        int v = life_of(a);
        if (code < lo || code > hi || v < 1 || v >= 0x7000) continue;
        ng.ram[(a + game_profile.object_life) & 0xFFFF] = 0;
        ng.ram[(a + game_profile.object_life + 1) & 0xFFFF] = 0;
        ng.ram[(a + game_profile.object_dead) & 0xFFFF] |= dead_mask();
    }
}

/* ------------------------------------------------------------------ carried values */
static uint32_t field8(uint32_t offset) { return game_profile.player_table + offset; }

void ffwd_carry_read(FfCarry *c)
{
    for (int i = 0; i < 4; i++) c->score[i] = ng.ram[(game_profile.score_p1 + i) & 0xFFFF];
    for (int i = 0; i < 8; i++)
        c->digits[i] = game_profile.score_p1_digits ? ng.ram[(game_profile.score_p1_digits + i) & 0xFFFF] : 0;
    if (game_profile.score_p1_digits)                 /* the BCD copy can be older */
        for (int i = 0; i < 4; i++) c->score[i] = (uint8_t)((c->digits[2 * i] & 15) << 4 | (c->digits[2 * i + 1] & 15));
    c->lives = ng.ram[game_profile.player1_lives & 0xFFFF];
    c->bombs = ng.ram[field8(game_profile.table_bombs) & 0xFFFF];
    c->weapon = ng.ram[field8(game_profile.table_weapon) & 0xFFFF];
    c->ammo[0] = ng.ram[field8(game_profile.table_ammo) & 0xFFFF];
    c->ammo[1] = ng.ram[(field8(game_profile.table_ammo) + 1) & 0xFFFF];
}

void ffwd_carry_write(const FfCarry *c)
{
    for (int i = 0; i < 4; i++) ng.ram[(game_profile.score_p1 + i) & 0xFFFF] = c->score[i];
    if (game_profile.score_p1_digits)
        for (int i = 0; i < 8; i++) ng.ram[(game_profile.score_p1_digits + i) & 0xFFFF] = c->digits[i];
    ng.ram[game_profile.player1_lives & 0xFFFF] = c->lives;
    ng.ram[field8(game_profile.table_bombs) & 0xFFFF] = c->bombs;
    ng.ram[field8(game_profile.table_weapon) & 0xFFFF] = c->weapon;
    ng.ram[field8(game_profile.table_ammo) & 0xFFFF] = c->ammo[0];
    ng.ram[(field8(game_profile.table_ammo) + 1) & 0xFFFF] = c->ammo[1];
}

/* ------------------------------------------------------------------ the run */
#define TARGETS 8
static const ProfileRushState *run;     /* the mission's bosses, in order */
static int n_targets, target;
static uint64_t start, played, since;    /* power-on frame; frames since the player appeared, since the last boss */
static uint32_t rnd;
static uint8_t seg_joy, seg_jump_at, seg_jump_len, seg_up, seg_grenade;
static struct { uint8_t *data; size_t size; uint64_t at; } ring[RING];
static uint8_t *result[TARGETS];
static size_t result_size[TARGETS];
static FfCarry new_game;
static int have_new_game, in_game, saved_dips, saved_devmode, heavy;
int ffwd_kill_max = 300;

static uint32_t next_rnd(void)           /* xorshift32 */
{
    rnd ^= rnd << 13;
    rnd ^= rnd >> 17;
    rnd ^= rnd << 5;
    return rnd;
}

static int percent(int p) { return (int)(next_rnd() % 100) < p; }

int ffwd_boss_present(const ProfileRushState *r)
{
    const ProfileBoss *b = &game_profile.bosses[r->boss];
    int min = game_profile.boss_min_life ? game_profile.boss_min_life : 1000;
    uint32_t head = game_profile.object_list, a = head;
    for (int n = 0; n < 400; n++) {
        a = l32(a + 8);
        if (a == head || a < 0x100000 || a >= 0x10F000) break;
        uint32_t code = l32(a);
        int v = life_of(a);
        if (code >= b->code_lo && code <= b->code_hi && v >= min && v < 0x7000 &&
            !(ng.ram[(a + game_profile.object_dead) & 0xFFFF] & dead_mask()))
            return 1;
    }
    return 0;
}

/* The profile's other named bosses (middle bosses, earlier targets) die too, whatever their life:
   only the target is fought. Not the unnamed ones: Mission 2's boats killed this way hang the game. */
static void kill_other_bosses(const ProfileBoss *keep)
{
    uint32_t head = game_profile.object_list, a = head;
    for (int n = 0; n < 400; n++) {
        a = l32(a + 8);
        if (a == head || a < 0x100000 || a >= 0x10F000) break;
        uint32_t code = l32(a);
        int v = life_of(a);
        if ((code >= keep->code_lo && code <= keep->code_hi) || v < 1 || v >= 0x7000) continue;
        for (int i = 0; i < game_profile.n_bosses; i++) {
            const ProfileBoss *b = &game_profile.bosses[i];
            if (!b->name || code < b->code_lo || code > b->code_hi) continue;
            ng.ram[(a + game_profile.object_life) & 0xFFFF] = 0;
            ng.ram[(a + game_profile.object_life + 1) & 0xFFFF] = 0;
            ng.ram[(a + game_profile.object_dead) & 0xFFFF] |= dead_mask();
            break;
        }
    }
}

static void free_ring(void)
{
    for (int i = 0; i < RING; i++) { free(ring[i].data); ring[i].data = NULL; }
}

void ffwd_end(void)
{
    free_ring();
    for (int i = 0; i < TARGETS; i++) { free(result[i]); result[i] = NULL; }
    if (run) {
        cheats_dips = (uint16_t)saved_dips;
        cheats_devmode = saved_devmode;
        cheats_start_mission_once = -1;
        cheats_mission_from = 0;
        cheats_apply();
    }
    run = NULL;
}

void ffwd_begin(const ProfileRushState *r, int count)
{
    ffwd_end();
    saved_dips = cheats_dips;
    saved_devmode = cheats_devmode;
    run = r;
    n_targets = count < 1 ? 1 : count > TARGETS ? TARGETS : count;
    target = 0;
    rnd = r->seed ? r->seed : 1;
    played = since = 0;
    have_new_game = in_game = 0;
    heavy = -1;
    for (int w = 0; w < cheats_weapons(); w++)
        if (cheats_weapon_name(w) && !strcmp(cheats_weapon_name(w), "Heavy Machine Gun")) heavy = w;
    /* the same run on every machine: the backup RAM as from the factory (the BIOS writes its
       defaults: the soft dips' difficulty and lives) and a fixed clock */
    ng_power_on();
    memset(ng.sram, 0, sizeof ng.sram);
    ng_rtc_set(2026, 10, 25, 0, 12, 0, 0);
    start = ng.frames;
    /* Mission mode's direct start (cheats.h). Not the invincibility switch: with it a fall is
       undone without the HUD's respawn, which then stops updating (and a mission never ends). */
    cheats_start_mission_once = r->mission;
    cheats_start_section_once = -1;
    cheats_mission_from = start + game_profile.boot_frames;
    cheats_devmode = 0;
    cheats_dips = 0;
    cheats_apply();
}

/* The inputs for the frame about to run: the coin and START after the boot, then the bot (the
   current target's parameters). */
static void inputs(void)
{
    const ProfileRushState *r = &run[target];
    uint64_t f = ng.frames - start, boot = game_profile.boot_frames;
    uint8_t joy = 0, sys = 0;
    if (f >= boot + 1 && f <= boot + 6) sys |= NG_COIN;
    if (f >= boot + 51 && f <= boot + 56) sys |= NG_START;
    if (have_new_game) {
        uint64_t k = played % SEGMENT;
        if (k == 0) {
            int d = (int)(next_rnd() % 100);
            seg_joy = d < r->right ? NG_RIGHT : d < r->right + r->left ? NG_LEFT : 0;
            seg_jump_at = percent(r->jump) ? (uint8_t)(next_rnd() % 20) : 0xFF;
            static const uint8_t len[3] = {4, 10, 18};
            seg_jump_len = len[next_rnd() % 3];
            seg_up = (uint8_t)percent(25);
            seg_grenade = (uint8_t)percent(r->grenade);
        }
        joy = seg_joy;
        if (seg_jump_at != 0xFF && k >= seg_jump_at && k <= (uint64_t)seg_jump_at + seg_jump_len) joy |= NG_B;
        if (seg_up && k >= 5 && k <= 30) joy |= NG_UP;
        if (seg_grenade && k >= 20 && k <= 23) joy |= NG_C;
        if (k % 6 <= 2) joy |= NG_A;
    }
    ng.joy[0] = joy;
    ng.joy[1] = 0;
    ng.system[0] = sys;
    ng.system[1] = 0;
    ng.test = 0x80;
}

int ffwd_run(int frames)
{
    if (!run) return FF_FAILED;
    if (target >= n_targets) return FF_DONE;
    for (int n = 0; n < frames; n++) {
        const ProfileRushState *r = &run[target];
        const ProfileBoss *b = &game_profile.bosses[r->boss];
        inputs();
        ng_run_frame();
        cheats_frame();
        if (!have_new_game) {
            /* the player has appeared (2 s ago: the table is set by then): a new game's values,
               then the bot starts */
            in_game = cheats_in_game(0) && ng.ram[game_profile.mission_now & 0xFFFF] == r->mission ? in_game + 1 : 0;
            if (in_game >= 120) {
                ffwd_carry_read(&new_game);
                memset(new_game.score, 0, sizeof new_game.score);
                memset(new_game.digits, 0, sizeof new_game.digits);
                have_new_game = 1;
            } else if (ng.frames - start > 20000) return FF_FAILED;
            continue;
        }
        played++;
        since++;
        ffwd_kill(ffwd_kill_max, b->code_lo, b->code_hi);
        kill_other_bosses(b);
        if (played % REFILL == 60 && cheats_in_game(0)) {
            cheats_set_lives(0, 9);
            cheats_set_bombs(0, 30);
            if (heavy >= 0) cheats_give_weapon(0, heavy);
        }
        if (played % RING_EVERY == 0) {
            int i = (int)(played / RING_EVERY % RING);
            free(ring[i].data);
            ring[i].data = ng_state_save(&ring[i].size);
            ring[i].at = played;
        }
        if (ffwd_boss_present(r)) {
            /* the newest state at least BEFORE frames old; without one, the oldest */
            int best = -1, oldest = -1;
            for (int i = 0; i < RING; i++) {
                if (!ring[i].data) continue;
                if (ring[i].at + BEFORE <= played && (best < 0 || ring[i].at > ring[best].at)) best = i;
                if (oldest < 0 || ring[i].at < ring[oldest].at) oldest = i;
            }
            if (best < 0) best = oldest;
            if (best < 0) return FF_FAILED;
            result[target] = ring[best].data;
            result_size[target] = ring[best].size;
            ring[best].data = NULL;
            free_ring();                         /* the next boss's state comes after this one */
            since = 0;
            if (++target >= n_targets) return FF_DONE;
            continue;
        }
        if (since >= r->max_frames) return FF_FAILED;
    }
    return FF_RUNNING;
}

int ffwd_found(void) { return target; }

float ffwd_progress(void)
{
    if (!run) return 0;
    if (target >= n_targets) return 1;
    float p = run[target].max_frames ? (float)since / (float)run[target].max_frames : 0;
    return ((float)target + (p > 1 ? 1 : p)) / (float)n_targets;
}

const uint8_t *ffwd_state(int i, size_t *size, FfCarry *c)
{
    if (i < 0 || i >= n_targets || !result[i]) return NULL;
    *size = result_size[i];
    if (c) *c = new_game;
    return result[i];
}
