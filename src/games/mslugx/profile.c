/* Metal Slug X: the frontend's game profile (cheats, debug options, logo, sound names).
 *
 * Data found in the GOG copy's program during the Metal Slug X port (P1 listing) and confirmed
 * with the recorded Mission 1 playthrough. Addresses and hashes only, never game data.
 */
#include "profile.h"

/* Code that must be identical to the GOG copy's for this profile to be used (profile.c). */
static const ProfileWindow WINDOWS[] = {
    {0x002580, 0x0025A0},     /* debug dips cleared without the development mode (0x2592) */
    {0x0039B0, 0x003A42},     /* VBlank wait loop (wait_start/wait_end) */
    {0x0040BC, 0x004100},     /* object allocation (vehicle spawn) */
    {0x015400, 0x015410},     /* development mode copied to 0x10C2BE (0x15408) */
    {0x0197DC, 0x019AF8},     /* object type table (vehicle types 0x2EC-0x318) */
    {0x01A0A6, 0x01A140},     /* stage object records (spawn_routine) */
    {0x01B2B8, 0x01B2C8},     /* BLOOD option read (0x1B2BE) */
    {0x0C07F0, 0x0C0A00},     /* weapon tables and pickup routine */
    {0x0C0BAE, 0x0C0BF4},     /* fat check (fat_check_first/last) */
    {0x0C32E4, 0x0C3320},     /* form transitions */
    {0x0C942C, 0x0C9440},     /* lives from the HERO soft dip */
};

/* Vehicles: object types (type*4 in the table at 0x197DC) found by spawning each type of the
   table in Mission 1; the SV-001 record is the one Mission 1 uses (frame 6065). Several are not
   used by Metal Slug X's missions as far as we know. */
static const CheatVehicle VEHICLES[] = {
    {0x2EC, "Metal Slug (SV-001)"}, {0x2F0, "Metal Slug (gold)"}, {0x2F4, "Camel Slug"},
    {0x2F8, "Slug Flyer"}, {0x2FC, "Slugnoid"}, {0x300, "Ostrich Slug"}, {0x304, "Elephant Slug"},
    {0x308, "Slug Copter"}, {0x30C, "Pod (unknown)"}, {0x310, "Green tank (unknown)"},
    {0x314, "Mounted gun"}, {0x318, "Walker (unknown)"},
};

/* Identified by their shots and by the announcer voice each one plays (table 0xC08F8); 7-11 share
   the voice of 2-6 with bigger shots; 1 has no voice and is not picked up in the game as far as we
   know. */
static const char *const WEAPONS[] = {
    "Pistol", "Big blast (unused?)", "Shotgun", "Rocket Launcher", "Flame Shot",
    "Heavy Machine Gun", "Laser Shot", "Big Shotgun", "Big Rocket Launcher", "Big Flame Shot",
    "Big Heavy Machine Gun", "Big Laser Shot", "Enemy Chaser", "Iron Lizard", "Drop Shot",
    "Super Grenade",
};

/* Transformations: player object +0x93 (0 / 1 normal, 2 fat, 3 mummy). */
static const CheatForm FORMS[] = {{0, "Normal"}, {2, "Fat"}, {3, "Mummy"}};

/* Rumble: effects (mode 10) identified in Mission 1. Low = big motor (thumps), high = small motor
   (buzz). */
static const SoundRumble RUMBLE[] = {
    {0x22, 0xFFFF, 0xA000, 380},                 /* big explosion */
    {0xF2, 0xE000, 0x9000, 300},                 /* armor hit */
    {0x25, 0xC000, 0x8000, 260},                 /* explosion */
    {0x21, 0xC000, 0x8000, 260},                 /* explosion */
    {0xA3, 0xB000, 0x7000, 240},                 /* explosion */
    {0xF5, 0xA000, 0x6000, 200},                 /* grenade or mortar explosion */
    {0x84, 0x9000, 0x7000, 140},                 /* Slug cannon */
    {0x8C, 0x2800, 0x5000, 45},                  /* Slug vulcan */
    {0x8D, 0x2000, 0x4000, 40},                  /* burst */
    {0xF7, 0x1800, 0x3000, 35},                  /* pistol */
    /* tuned by the user (Phase 8) */
    {0x27, 0xE000, 0x9800, 320},                 /* grenade explosion */
    {0x2F, 0xE000, 0x9800, 320},                 /* explosion */
    {0xF8, 0xE000, 0x9800, 320},                 /* laser */
    {0xF4, 0x9000, 0x7000, 150},                 /* (medium) */
    {0x48, 0x2000, 0x4000, 45},                  /* rope cut */
};

extern const Sound MSLUGX_SOUNDS[];       /* sounds.c (generated) */
extern const int MSLUGX_SOUND_COUNT;

const GameProfile game_profile = {
    .title = "METAL SLUG X",
    .subtitle = "SUPER VEHICLE-001",
    .windows = WINDOWS,
    .n_windows = (int)(sizeof WINDOWS / sizeof WINDOWS[0]),
    .windows_sha256 = "4c0665fb63e1336e57dc521de523f416e8131aa249a55bcc836388a4a1c6a38f",
    .vehicles = VEHICLES,
    .n_vehicles = (int)(sizeof VEHICLES / sizeof VEHICLES[0]),
    .sounds = MSLUGX_SOUNDS,
    .n_sounds = &MSLUGX_SOUND_COUNT,
    .rumble = RUMBLE,
    .n_rumble = (int)(sizeof RUMBLE / sizeof RUMBLE[0]),

    /* Main loop: waits for VBlank at 0x39B0-0x3A3E (bclr #7,-$7F75(a5); beq) and exits at 0x3A42. */
    .wait_start = 0x39B0u,
    .wait_end = 0x3A42u,

    /* On leaving the wait, the game stores at -$7F76(a5) = 0x10008A how many VBlanks the logic cycle
     * took: normally 2 (30 Hz), 3 or 4 with slowdown (20 or 15 Hz). Offset in RAM. */
    .ram_frames_per_tick = 0x008Au,

    /* Blood: the game options in the BIOS service menu (soft dips, table in the P ROM header)
     * include "BLOOD OFF/ON"; in the US and Europe it defaults to OFF (white blood). The BIOS
     * copies the options to 0x10FD84: two times, two counts and ten options from 0x10FD8A
     * (CONTINUE, DIFFICULTY, PLAY TIME, DEMO SOUND, PLAY MANUAL, BLOOD...). The game checks it at
     * 0x1B2BE (tst.b $10FD8F). Writing that byte from power-on breaks the BIOS RAM test: only the
     * game code's read is forced (ng_forced_read, low byte of the word). */
    /* Game debug dips: two bytes that only take effect with the BIOS development mode
     * (0x10FE80); otherwise the game clears them every frame (0x2592). Forced like the blood. */
    .dips_word = 0x10F000,
    /* BIOS development mode (high byte of the word): opens the developers' hidden menu. */
    .devmode_word = 0x10FE80,
    /* The game copies the development mode flag to 0x10C2BE when a game starts (0x15408) and the
     * pause code checks the copy: forced too (high byte), so the mode works mid-mission. */
    .devmode_copy_word = 0x10C2BE,
    .dev_menu = DEV_MENU_SELECT_D,

    /* Game state (0x100088): 2 while a mission is being played. */
    .game_state = 0x100088u,
    .game_state_playing = 2,
    /* Per-player table at 0x10C718 (bytes interleaved P1, P2; words at +0xC P1, +0xE P2):
     * +0 0 on foot, 2 in the Slug (0xC07F8), +2 shot counter, +4 weapon sub-type, +6 weapon, +8 bombs,
     * +0xA bomb type, +0xC ammo (0xFFFF = infinite). */
    .player_table = 0x10C718u,
    /* Player objects (pickup routine 0xC0928: +0x94 weapon flag, +0x95 cleared). */
    .player1_object = 0x1014C4u,
    .player2_object = 0x101574u,
    .object_character = 0x6F,        /* internal character: Marco 0, Tarma 1, Eri 2, Fio 3 */
    .object_form = 0x93,             /* 0 normal, 2 fat, 3 mummy; +0x94 = requested form */
    .fat_food = 0x780,               /* food counter (table +0x10) that makes the soldier fat */
    .table_food = 0x10,
    .forms = FORMS,
    .n_forms = (int)(sizeof FORMS / sizeof FORMS[0]),
    .rom_weapon_form = 0xC0918u,     /* the pickup flag: 0 pistol, 1 other weapons */
    .form_help = "Fat lasts until you die or change it; mummy until cured",
    /* While fat, each tick the routine 0xC0BAE-0xC0BF2 checks the stage (word 0x10C2F6): only in
     * stages 4-7, 9 and 10 (or with a food object, 0x10E9F8) the counter runs down slowly; elsewhere
     * the soldier slims down at once. The fat cheat makes that routine read stage 4. */
    .stage_word = 0x10C2F6u,
    .fat_check_first = 0xC0BAEu,
    .fat_check_last = 0xC0BF2u,
    .fat_stage = 4,
    /* Stage object records: routine 0x1A0A6 (A0 = record list, A6 = the stage's object root). */
    .spawn_routine = 0x1A0A6u,
    .spawn_parent = 0x101414u,
    .spawn_x = 0x100,                 /* the stage's SV-001 record: x 256 (on screen), y 96 */
    .spawn_y = 0x60,
    .spawn_head = {0x07, 0x0A, 0x09, 0x80},   /* group 9: none; flags 0x80: x on the screen */
    .spawn_type_at = 4,
    .spawn_x_at = 6,
    .spawn_y_at = 8,
    /* Player status objects 0x101204 / 0x1012B4: +0x83 soldier (0 Marco, 1 Eri, 2 Tarma, 3 Fio,
     * soldier select order), +0x82 the same as internal character, +0x84 lives (from the HERO
     * soft dip, 0xC942C). */
    .player1_soldier = 0x101287u,
    .player2_soldier = 0x101337u,
    .player1_lives = 0x101288u,
    .player2_lives = 0x101338u,
    /* Weapon tables in the P ROM, indexed by weapon (0-15), used by the pickup routine. */
    .rom_weapon_subtype = 0xC08A8u,  /* bytes */
    .rom_weapon_ammo = 0xC08B8u,     /* words: ammo on pickup */
    .rom_weapon_flag = 0xC0918u,     /* bytes: player object +0x94 */
    .rom_weapon_subtype_stride = 1,
    .rom_weapon_ammo_stride = 2,
    .weapons = sizeof WEAPONS / sizeof WEAPONS[0],
    .weapon_names = WEAPONS,
    /* Player table 0x10C718: both players interleaved. */
    .table_stride_byte = 1,
    .table_stride_word = 2,
    .table_shots = 2,
    .table_subtype = 4,
    .table_weapon = 6,
    .table_bombs = 8,
    .table_ammo = 0xC,
    .blood_word = 0x10FD8Eu,
    .blood_mask = 0x00FFu,

    /* Title screen logo (attract, frame ~1150 from power-on with empty SRAM):
     * sprites 41-124 (shadow, "METAL SLUG", X, "SUPER VEHICLE-001") and 380 ("TM", first tile 0x309CA,
     * used to recognize the moment). The red background is sprites 21-40. */
    .logo_first = 41,
    .logo_last = 124,
    .logo_tm = 380,
    .logo_tm_tile = 0x309CAu,
    .logo_max_frames = 3000,
    /* Fast boot: the BIOS boot screen, black, and the game's first image at frame 304. */
    .boot_frames = 304,

    /* Debug dips: bit = the switch in the developers' "SET DEBUG DIP" menu. */
    .dips = {
        .invincible = 0x8000,
        .infinite_bombs = 0x0004,
        .infinite_ammo = 0x0008,
        .d_kills_all = 0x0001,             /* D button: every enemy on screen dies */
        .boxes = 0x2000 | 0x4000,          /* VIEW ATTACK RECT, VIEW BODY RECT */
        .info = 0x0100,                    /* VIEW INFOMATION: progress, scroll, enemies */
        .ground = 0x0800 | 0x1000,         /* VIEW GEO SCROLL A / B: ground lines */
        .cpu_meter = 0x0400,               /* not in the menu: darkens the screen while processing */
        .pa_point = 0x0002,                /* VIEW PA POINT */
        .pause_anytime = 0x0200,
    },
};
