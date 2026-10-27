/* Debug mode and cheats (Phase 7B): see cheats.h. Addresses in profile.h. */
#include "cheats.h"

#include <stdio.h>
#include <string.h>

#include "neogeo.h"
#include "profile.h"

uint16_t cheats_dips;
static int keep_fat[2];
int cheats_devmode;
int cheats_start_mission;
int cheats_start_mission_once = -1;
int cheats_start_section_once = -1;
uint64_t cheats_mission_from;
FILE *cheats_spawn_log;

const char *const CHEATS_SOLDIER_NAME[4] = {"Marco", "Eri", "Tarma", "Fio"};

static uint8_t *ram8(uint32_t a) { return &ng.ram[a & 0xFFFF]; }

/* The cheats' RAM writes; with PD_SPAWN_POKES they are logged as PD_POKE entries (frame - 1: the
   cheats run before a frame or right after it, where a MAME run's PD_POKE of that frame lands). */
static void poke8(uint32_t a, uint32_t v)
{
    *ram8(a) = (uint8_t)v;
    if (cheats_spawn_log) {
        fprintf(cheats_spawn_log, "%llu,%06X,%02X;", (unsigned long long)ng.frames - 1, 0x100000u | (a & 0xFFFF), (uint8_t)v);
        fflush(cheats_spawn_log);
    }
}
static void poke16(uint32_t a, uint16_t v) { poke8(a, v >> 8); poke8(a + 1, v & 0xFF); }
static uint8_t rom8(uint32_t a) { return ng_roms.p[a]; }
static uint16_t rom16(uint32_t a) { return (uint16_t)(ng_roms.p[a] << 8 | ng_roms.p[a + 1]); }

/* Everything here relies on profile.h: with another version of the game program
   (profile_supported() == 0) nothing is forced and the cheats do nothing. */
void cheats_apply(void)
{
    int ok = profile_supported();
    ng_forced_read[NG_FORCED_DIPS] = (NgForcedRead){ok && cheats_dips != 0, game_profile.dips_word, cheats_dips, cheats_dips, 0, 0};
    ng_forced_read[NG_FORCED_DEVMODE] = (NgForcedRead){ok && cheats_devmode, game_profile.devmode_word, 0xFF00, 0x0100, 0, 0};
    ng_forced_read[NG_FORCED_DEVMODE_COPY] = (NgForcedRead){ok && cheats_devmode, game_profile.devmode_copy_word, 0xFF00, 0x0100, 0, 0};
    ng_forced_read[NG_FORCED_FAT_STAGE] = (NgForcedRead){ok && game_profile.fat_check_first && (keep_fat[0] || keep_fat[1]), game_profile.stage_word, 0xFFFF,
                                                         game_profile.fat_stage, game_profile.fat_check_first, game_profile.fat_check_last};
}

static uint32_t object_of(int p) { return p ? game_profile.player2_object : game_profile.player1_object; }

/* Player table fields (profile.h: table layout). */
static uint32_t field8(int p, uint32_t offset) { return game_profile.player_table + offset + (uint32_t)p * game_profile.table_stride_byte; }
static uint32_t field16(int p, uint32_t offset) { return game_profile.player_table + offset + (uint32_t)p * game_profile.table_stride_word; }

static uint32_t ram32(uint32_t a) { return (uint32_t)ram8(a)[0] << 24 | ram8(a)[1] << 16 | ram8(a + 2)[0] << 8 | ram8(a + 2)[1]; }

int cheats_in_game(int player)
{
    if (!profile_supported()) return 0;
    if (game_profile.game_state) return *ram8(game_profile.game_state) == game_profile.game_state_playing;
    int p = player & 1;
    return *ram8(0x10FDB6 + p) == 1 && ram32(object_of(p) + game_profile.object_table_ptr) == field8(p, 0);
}

int cheats_weapons(void) { return (int)game_profile.weapons; }

static int spawn_item(int kind);
static int normal_form(int p);
static int give_player = -1, give_weapon = -1;

/* With weapon items (Metal Slug 3): the weapon's item appears in front of player 1 and the game
 * gives it when picked up (sprite, announcer, ammo). Otherwise, the same effect as the game's own
 * pickup routine (Metal Slug X 0xC0928, Metal Slug 3 0x21DC8) minus the announcer: weapon, its
 * sub-type, initial ammo and the player object's flags, all from the game's tables in the ROM,
 * and the normal form for the new weapon requested (the pistol or a weapon in the hands). */
int cheats_give_pending(void) { return give_player >= 0; }

int cheats_give_weapon(int player, int weapon)
{
    if (!cheats_in_game(player) || weapon < 0 || weapon >= cheats_weapons()) return 0;
    if (game_profile.item_type && game_profile.weapon_items[weapon]) return spawn_item(game_profile.weapon_items[weapon]);
    if (game_profile.give_routine) {             /* the game's own routine (Metal Slug 5) */
        give_player = player & 1;
        give_weapon = weapon;
        return 1;
    }
    int p = player & 1;
    uint32_t obj = object_of(p);
    uint32_t flag = game_profile.object_pickup_flag ? game_profile.object_pickup_flag : 0x94;
    uint8_t *now = ram8(field8(p, game_profile.table_weapon));
    if (game_profile.table_previous_weapon && *now != weapon)
        poke8(field8(p, game_profile.table_previous_weapon), *now);
    poke8(field8(p, game_profile.table_weapon), (uint32_t)weapon);
    poke8(field8(p, game_profile.table_subtype), rom8(game_profile.rom_weapon_subtype + weapon * game_profile.rom_weapon_subtype_stride));
    poke8(field8(p, game_profile.table_shots), 0);
    poke16(field16(p, game_profile.table_ammo), rom16(game_profile.rom_weapon_ammo + weapon * game_profile.rom_weapon_ammo_stride));
    poke8(obj + flag, game_profile.rom_weapon_flag ? rom8(game_profile.rom_weapon_flag + weapon) : 0);
    if (game_profile.rom_weapon_flag) poke8(obj + flag + 1, 0);
    if (game_profile.rom_weapon_form && cheats_form(p) <= 1)        /* normal: 0 pistol, 1 weapon */
        poke8(obj + game_profile.object_form + 1, (uint8_t)normal_form(p));
    return 1;
}

int cheats_set_bombs(int player, int n)
{
    if (!cheats_in_game(player)) return 0;
    poke8(field8(player & 1, game_profile.table_bombs), (uint8_t)(n < 0 ? 0 : n > 99 ? 99 : n));
    return 1;
}

int cheats_set_lives(int player, int n)
{
    if (!cheats_in_game(player)) return 0;
    poke8((player & 1) ? game_profile.player2_lives : game_profile.player1_lives, (uint8_t)(n < 0 ? 0 : n > 9 ? 9 : n));
    return 1;
}

/* The soldier select index (Marco, Eri, Tarma, Fio) and the internal character number
   (Marco 0, Tarma 1, Eri 2, Fio 3), unless the profile gives its own (soldier_internal_of). */
static const uint8_t DEFAULT_INTERNAL[4] = {0, 2, 1, 3};
#define INTERNAL (game_profile.soldier_internal_of ? game_profile.soldier_internal_of : DEFAULT_INTERNAL)

const char *cheats_soldier_name(int soldier)
{
    return (game_profile.soldier_names ? game_profile.soldier_names : CHEATS_SOLDIER_NAME)[soldier & 3];
}

int cheats_soldier(int player)
{
    int v = *ram8((player & 1) ? game_profile.player2_soldier : game_profile.player1_soldier) & 3;
    if (!game_profile.soldier_internal) return v;
    for (int s = 0; s < 4; s++)
        if (INTERNAL[s] == v) return s;
    return 0;
}

int cheats_set_soldier(int player, int soldier)
{
    if (!cheats_in_game(player)) return 0;
    int p = player & 1, s = soldier & 3;
    uint32_t status = p ? game_profile.player2_soldier : game_profile.player1_soldier;
    if (game_profile.soldier_internal) {           /* the status object's character: next life */
        poke8(status, INTERNAL[s]);
        return 1;
    }
    poke8(status, (uint8_t)s);
    poke8(status - 1, INTERNAL[s]);
    poke8((p ? game_profile.player2_object : game_profile.player1_object) + game_profile.object_character, INTERNAL[s]);
    return 1;
}

/* Transformations (player object: object_form current form, the next byte the requested one).
   Fat: the food routine (Metal Slug X 0xC0BF4, Metal Slug 3 0x22176) adds to the player's food
   counter (table_food) and requests form 2 at 0x780; the game then plays the transformation. The
   other forms are requested the same way. */
/* A form as the cheat sees it: with the game's form table (Metal Slug 5), every fat form is
   FORM_FAT (its fat form depends on the weapon class). */
static int form_kind(int form)
{
    if (game_profile.form_table && rom8(game_profile.form_class + (uint32_t)form) == game_profile.fat_class) return FORM_FAT;
    return form;
}

int cheats_form(int player)
{
    return form_kind(*ram8(((player & 1) ? game_profile.player2_object : game_profile.player1_object) + game_profile.object_form));
}

/* With the game's form table: the form the game itself would request for that event (1 fat,
   2 back to normal) from the current form and weapon. */
static int table_form(int p, int event)
{
    uint32_t obj = p ? game_profile.player2_object : game_profile.player1_object;
    uint32_t w = *ram8(field8(p, game_profile.table_weapon));
    int wclass = rom8(game_profile.form_weapon_class + (w < game_profile.weapons ? w : 0));
    int fclass = rom8(game_profile.form_class + *ram8(obj + game_profile.object_form));
    int wclasses = game_profile.form_weapon_classes ? game_profile.form_weapon_classes : 3;
    int fclasses = game_profile.form_classes ? game_profile.form_classes : 4;
    return rom8(game_profile.form_table + (uint32_t)((event * wclasses + wclass) * fclasses + fclass));
}

/* Normal form is 0 with the pistol and 1 with (most) other weapons: a table in the ROM. */
static int normal_form(int p)
{
    uint32_t w = *ram8(field8(p, game_profile.table_weapon));
    return rom8(game_profile.rom_weapon_form + (w < game_profile.weapons ? w : 0));
}

/* With the form table: the event that asks for `form` (back to normal: the current form's own
   way back, form_events). */
static int form_event(int form, int now)
{
    if (!game_profile.form_events) return form == FORM_FAT ? 1 : 2;
    for (int i = 1; i < game_profile.n_forms; i++)
        if (game_profile.forms[i].form == (form == FORM_NORMAL ? form_kind(now) : form))
            return game_profile.form_events[i - 1][form == FORM_NORMAL ? 1 : 0];
    return 2;
}

int cheats_set_form(int player, int form)
{
    if (!cheats_in_game(player)) return 0;
    int p = player & 1;
    uint32_t obj = p ? game_profile.player2_object : game_profile.player1_object;
    int now = *ram8(obj + game_profile.object_form);
    if (game_profile.form_normal_first && now > FORM_FAT) form = FORM_NORMAL;      /* 0 / 1 normal, 2 fat */
    keep_fat[p] = form == FORM_FAT;
    poke16(field16(p, game_profile.table_food), keep_fat[p] ? game_profile.fat_food : 0);
    int request = game_profile.form_table ? table_form(p, form_event(form, now)) : form == FORM_NORMAL ? normal_form(p) : form;
    poke8(obj + game_profile.object_form + 1, (uint8_t)request);
    cheats_apply();
    return 1;
}

/* ------------------------------------------------------------------ spawning objects */
/* The stages create their objects from records read by a routine of the game (spawn_routine):
   Metal Slug X 0x1A0A6, command 7: [7, length, group, flags, type*4, x, y, parameters...] (flags
   bit 7: x relative to the screen); Metal Slug 3 0x16106, command 8: [8, length, group, flags,
   x, y, type*4, parameters...] (flags 0: x and y on the screen). The port builds such a record
   (spawn_head and the type, x and y words) below the game's stack and calls the routine
   (ng_call68k) while the game waits for the VBlank, so the object is created exactly as the stage
   would. */
static int spawn_type = -1, spawn_param = -1;

int cheats_spawn_type(int type)
{
    if (!cheats_in_game(0)) return 0;
    spawn_type = type;
    spawn_param = -1;
    return 1;
}

/* Item records carry parameters: [kind, 0xFFFF, 0, 0, 0] (as the stages' items: no time limit,
   the object's first variant). */
static int spawn_item(int kind)
{
    if (!cheats_in_game(0)) return 0;
    spawn_type = (int)game_profile.item_type;
    spawn_param = kind;
    return 1;
}

int cheats_spawn_vehicle(int vehicle)
{
    return vehicle >= 0 && vehicle < game_profile.n_vehicles && cheats_spawn_type(game_profile.vehicles[vehicle].type);
}

/* Calls a routine of the game with its stack (and the spawn record, `len` bytes of r) below the
   game's stack, and puts that memory back afterwards: only the routine's effects stay (the game
   may use that area later, e.g. the VBlank interrupt). Logs the RAM the call changed as PD_POKE
   entries (PD_SPAWN_POKES). */
static void call_game(uint32_t routine, uint32_t d[8], uint32_t a[8], const uint8_t *r, int len)
{
    uint32_t sp = ng_cpu.a[7] & ~1u, base = sp - 0x400;
    uint8_t scratch[0x400];
    static uint8_t before[sizeof ng.ram];
    if (cheats_spawn_log) memcpy(before, ng.ram, sizeof ng.ram);
    for (uint32_t i = 0; i < sizeof scratch; i++) scratch[i] = *ram8(base + i);
    for (int i = 0; i < len; i++) bus_w8(base + (uint32_t)i, r[i]);
    if (len) a[0] = base;
    a[5] = 0x108000;
    a[7] = sp - 0x100;
    ng_call68k(routine, d, a, 2000000);
    for (uint32_t i = 0; i < sizeof scratch; i++) *ram8(base + i) = scratch[i];
    if (cheats_spawn_log) {
        for (uint32_t i = 0; i < sizeof ng.ram; i++)
            if (ng.ram[i] != before[i])
                fprintf(cheats_spawn_log, "%llu,%06X,%02X;", (unsigned long long)ng.frames - 1, 0x100000u + i, ng.ram[i]);
        fflush(cheats_spawn_log);
    }
}

/* The weapon cheat with the game's routine: D0 player, D1 weapon. */
static void give_now(int player, int weapon)
{
    uint32_t d[8] = {0}, a[8] = {0};
    d[0] = (uint32_t)player;
    d[1] = (uint32_t)weapon;
    call_game(game_profile.give_routine, d, a, NULL, 0);
}

/* A vehicle that is a fixed object (Metal Slug's at 0x100580): what the game's creation does
   (0x2AE58, 0x5DD02) when the object is free: its first state from the game's table, the position
   (player 1's, spawn_x ahead) and the cleared byte. The game's tasks run it from the next frame. */
static void vehicle_now(int type)
{
    uint32_t o = game_profile.vehicle_object, first = ram32(o);
    int free_slot = 0;
    for (int i = 0; i < 4; i++) free_slot |= first == game_profile.vehicle_free[i];
    if (!free_slot) return;                           /* already there */
    uint32_t state = (uint32_t)rom16(game_profile.vehicle_states + 4 * (uint32_t)type) << 16 |
                     rom16(game_profile.vehicle_states + 4 * (uint32_t)type + 2);
    uint32_t p = object_of(0);
    uint16_t x = (uint16_t)((uint16_t)(ram8(p + game_profile.vehicle_x_at)[0] << 8 | ram8(p + game_profile.vehicle_x_at)[1]) + game_profile.spawn_x);
    uint16_t y = (uint16_t)(ram8(p + game_profile.vehicle_y_at)[0] << 8 | ram8(p + game_profile.vehicle_y_at)[1]);
    poke16(o, (uint16_t)(state >> 16));
    poke16(o + 2, (uint16_t)state);
    poke16(o + game_profile.vehicle_x_at, x);
    poke16(o + game_profile.vehicle_y_at, y);
    poke16(o + game_profile.vehicle_y2_at, y);
    poke8(o + game_profile.vehicle_clear_at, 0);
}

static void spawn_now(int type, int param)
{
    if (game_profile.vehicle_object && param < 0) { vehicle_now(type); return; }
    uint8_t r[20] = {0};
    int len = param >= 0 ? 20 : 10;
    memcpy(r, game_profile.spawn_head, 4);
    r[1] = (uint8_t)len;
    if (param >= 0) { r[10] = (uint8_t)(param >> 8); r[11] = (uint8_t)param; r[12] = r[13] = 0xFF; }
    uint32_t x = param >= 0 ? game_profile.item_x : game_profile.spawn_x, y = game_profile.spawn_y;
    if (game_profile.spawn_layer) {
        uint32_t obj = object_of(0), pos = game_profile.object_position ? game_profile.object_position : 0x3C;
        x += (ram32(obj + pos) >> 16) - (ram32(game_profile.spawn_layer) >> 16);
        y += (ram32(obj + pos + 4) >> 16) - (ram32(game_profile.spawn_layer + 4) >> 16);
    }
    const struct { uint8_t at; uint32_t v; } w[3] = {
        {game_profile.spawn_type_at, (uint32_t)type}, {game_profile.spawn_x_at, x & 0xFFFF}, {game_profile.spawn_y_at, y & 0xFFFF}};
    for (int i = 0; i < 3; i++) { r[w[i].at] = (uint8_t)(w[i].v >> 8); r[w[i].at + 1] = (uint8_t)w[i].v; }
    uint32_t d[8] = {0}, a[8] = {0};
    a[6] = game_profile.spawn_parent;
    call_game(game_profile.spawn_routine, d, a, r, len);
}

/* Writes the start mission, and its copy where the profile has one (start_mission_also). */
static void write_start(uint8_t *m, int start)
{
    m[0] = (uint8_t)start;
    if (game_profile.start_mission_also) *ram8(game_profile.start_mission_also) = (uint8_t)start;
}

/* Writes the start section: the byte after the start mission, or the profile's start_section_at. */
static void write_section(uint8_t *m, int section)
{
    if (game_profile.start_section_at) *ram8(game_profile.start_section_at) = (uint8_t)section;
    else m[1] = (uint8_t)section;
}

/* The chosen start mission is loaded: the section byte set to 0xFF by the game, or, without one
   (start_no_section), the current mission is the chosen one with a player in the game. */
static int start_loaded(const uint8_t *m, int start)
{
    if (!game_profile.start_no_section) return m[1] == 0xFF;
    return *ram8(game_profile.mission_now) == start && (*ram8(0x10FDB6) == 1 || *ram8(0x10FDB7) == 1);
}

/* Every frame: keeps a fat soldier fat (food counter topped up) until he dies or the cheat is
   turned off. */
void cheats_frame(void)
{
    /* Start mission: when a new game has set the mission to start at to 0 and the mission is not
       loaded yet, the chosen one is written there, as the game's own memory card continue does
       (Metal Slug 3: docs/PHASE4_PROFILE.md). Only while a player is playing (BIOS player status
       at 0x10FDB6/0x10FDB7: 1 = playing): during the boot and the attract mode the bytes are 0 too. */
    int start = cheats_start_mission_once >= 0 ? cheats_start_mission_once : cheats_start_mission;
    /* Mission mode's direct start (cheats.h): written again whenever the game clears the bytes,
       until the mission is loaded (section byte 0xFF). Without it a single write could land on
       bytes the game clears again (the attract's leftovers, the "How to play"): mission 1. */
    int quick = cheats_mission_from && cheats_start_mission_once >= 0 && game_profile.manual_word &&
                game_profile.missions && profile_supported();
    ng_forced_read[NG_FORCED_MANUAL] = (NgForcedRead){quick && ng.frames >= cheats_mission_from, game_profile.manual_word,
                                                      (uint16_t)game_profile.manual_mask, 0, 0, 0};
    if (quick && ng.frames >= cheats_mission_from) {
        uint8_t *m = ram8(game_profile.start_mission);
        if (m[0] == 0 && m[1] == 0) {
            write_start(m, start);
            /* a section at its own byte (Metal Slug X: the Boss Rush fast-forward's) */
            if (game_profile.start_section_at && cheats_start_section_once >= 0) write_section(m, cheats_start_section_once);
        } else if (m[0] == start && start_loaded(m, start)) {       /* loaded */
            cheats_start_mission_once = -1;
            cheats_start_section_once = -1;
            cheats_mission_from = 0;
            ng_forced_read[NG_FORCED_MANUAL].active = 0;
        }
    } else if ((start > 0 || cheats_start_mission_once >= 0) && game_profile.missions && profile_supported() &&
        (*ram8(0x10FDB6) == 1 || *ram8(0x10FDB7) == 1)) {
        uint8_t *m = ram8(game_profile.start_mission);
        /* With a section (Boss Rush): written again whenever the game clears the bytes, until it is
           loaded (Metal Slug 4's "How to play" runs with the player playing and clears them after
           the first write). */
        int section = cheats_start_mission_once >= 0 ? cheats_start_section_once : -1;
        /* Without a section byte, a one-time start is kept until the mission is loaded. */
        int keep = section >= 0 || (cheats_start_mission_once >= 0 && game_profile.start_no_section);
        if (m[0] == 0 && m[1] == 0) {
            if (start > 0) write_start(m, start);
            if (section >= 0) write_section(m, section);
            else if (!keep) {
                cheats_start_mission_once = -1;
                cheats_start_section_once = -1;
            }
        } else if (keep && m[0] == start && start_loaded(m, start)) {       /* loaded */
            cheats_start_mission_once = -1;
            cheats_start_section_once = -1;
        }
    }
    /* Spawn requests wait for a quiet moment: the game waiting for the VBlank. */
    if (spawn_type >= 0 && ng_cpu.pc >= game_profile.wait_start && ng_cpu.pc < game_profile.wait_end) {
        if (cheats_in_game(0)) spawn_now(spawn_type, spawn_param);
        spawn_type = -1;
    }
    if (give_player >= 0 && ng_cpu.pc >= game_profile.wait_start && ng_cpu.pc < game_profile.wait_end) {
        if (cheats_in_game(give_player)) give_now(give_player, give_weapon);
        give_player = -1;
    }
    for (int p = 0; p < 2; p++) {
        if (!keep_fat[p]) continue;
        uint32_t obj = p ? game_profile.player2_object : game_profile.player1_object;
        int form = form_kind(*ram8(obj + game_profile.object_form)), pending = form_kind(*ram8(obj + game_profile.object_form + 1));
        if (!cheats_in_game(p) || (form != FORM_FAT && pending != FORM_FAT)) { keep_fat[p] = 0; cheats_apply(); continue; }
        poke16(field16(p, game_profile.table_food), game_profile.fat_food);
    }
}

const char *cheats_weapon_name(int weapon)
{
    return weapon >= 0 && weapon < cheats_weapons() ? game_profile.weapon_names[weapon] : NULL;
}

/* ------------------------------------------------------------------ developer menu */
/* Frame script relative to the request: one key held 0-29 with the other pressed 6-9 (pause),
 * again held 40-69 with the other pressed 46-49 (menu); which is held depends on the game
 * (game_profile.dev_menu). The game reads inputs once per logic tick (2 frames). */
static int dev_frame = -1;

void cheats_dev_menu(void)
{
    if (cheats_devmode) dev_frame = 0;
}

void cheats_input(uint8_t *joy, uint8_t *system)
{
    if (dev_frame < 0) return;
    int f = dev_frame++;
    int held = (f < 30) || (f >= 40 && f < 70), pressed = (f >= 6 && f < 10) || (f >= 46 && f < 50);
    int d_first = game_profile.dev_menu == DEV_MENU_D_SELECT;
    if (d_first ? pressed : held) *system |= NG_SELECT;
    if (d_first ? held : pressed) *joy |= NG_D;
    if (dev_frame >= 70) dev_frame = -1;
}
