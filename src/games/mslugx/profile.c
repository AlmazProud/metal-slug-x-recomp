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
    /* Phase 12: missions (docs/PHASE12_MISSIONS.md) */
    {0x016B9A, 0x016BE4},     /* a new game: stage set 0 (0x17F20), PLAY MANUAL read (0x16BD4) */
    {0x017F20, 0x017F4A},     /* stage set index 0x10C2FC, mission 0x10C2F4 = index mod 6 */
    {0x018292, 0x0182AC},     /* a mission loads: the stage set copied to 0x10C2F8 */
    {0x01891E, 0x01893E},     /* the next mission: 0x10C2F4 + 1, loaded below 6 */
    /* Phase 13: bosses (docs/BOSS_RUSH.md) */
    {0x0040BC, 0x00411C},     /* object allocation: the list's links (object_list) */
    {0x0046D0, 0x0046DA},     /* the kill-everything request copied each frame (kill_flag) */
    {0x004BAC, 0x004BFE},     /* an enemy's kill check: life 0x4E, dead bit 7 of 0x5F */
    {0x016D92, 0x016DB0},     /* the object list's root 0x1010A4 */
    {0x0D5A92, 0x0D5AB2},     /* the bosses' code (BOSSES): Mission 1 */
    {0x026354, 0x026374},     /* Mission 2 */
    {0x04A380, 0x04A3A0},     /* Mission 3 */
    {0x0235E8, 0x023608},     /* Mission 4 */
    {0x0A0C9A, 0x0A0CBA},     /* Mission 5 */
    {0x0415BA, 0x0415DA},     /* Mission 6 */
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

/* Bosses (docs/BOSS_RUSH.md): the objects that take the damage, found by fighting each boss from a
   state saved shortly before it (life +0x4E going down, a hit record at +0x50); NULL: no official
   name. Mission 1's tank has a body (5000) and a cannon (700); Mission 4's boss is its seven
   guns, 500 each. */
static const ProfileBoss BOSSES[] = {
    {0x0D5A92u, 0x0D67E6u, 5000, NULL, 0},                 /* Mission 1: the tank (body and cannon) */
    {0x026354u, 0x026FCEu, 3500, NULL, 0},                 /* Mission 2 (to its death routines) */
    {0x04A380u, 0x04ACDCu, 5500, NULL, 0},                 /* Mission 3 (to its death routines) */
    {0x0235E8u, 0x0241CCu, 0, NULL, 0},                    /* Mission 4: the guns (500 each) */
    {0x0A0C9Au, 0x0A0EE2u, 5000, NULL, 0},                 /* Mission 5 */
    {0x0415BAu, 0x0417FEu, 6500, NULL, 0},                 /* Mission 6 */
};

/* Boss Rush from saved states (fastfwd.h): each mission's boss, from a state the fast-forward saves
   shortly before it appears: its index in BOSSES, its mission (0-based), the mission ends with it,
   the bot (percentages of segments going right / left, jumping, throwing a grenade; seed; frames). */
static const ProfileRushState RUSH_STATES[] = {
    {0, 0, 1, 60, 15, 40, 10, 1, 20000, 0},
    {1, 1, 1, 60, 15, 40, 10, 1, 20000, 12},       /* from the last section: the pyramid stops every bot */
    {2, 2, 1, 60, 15, 40, 10, 1, 40000, 0},
    {3, 3, 1, 60, 15, 40, 10, 1, 30000, 0},
    {4, 4, 1, 60, 15, 40, 10, 1, 30000, 0},
    {5, 5, 1, 60, 15, 40, 10, 1, 40000, 0},
};

extern const Sound MSLUGX_SOUNDS[];       /* sounds.c (generated) */
extern const int MSLUGX_SOUND_COUNT;

const GameProfile game_profile = {
    .title = "METAL SLUG X",
    .subtitle = "SUPER VEHICLE-001",
    .windows = WINDOWS,
    .n_windows = (int)(sizeof WINDOWS / sizeof WINDOWS[0]),
    .windows_sha256 = "dbcfe7f3d333ac91fedf823f17513c8fa553e6193532f0418c86303aefb9c67c",
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

    /* Missions (docs/PHASE12_MISSIONS.md). The words 0x10C2F4-0x10C2FE:
     *   0x10C2F4 the mission (0-5; 0x10C2F5 its byte): a new game clears it (0x16B9E), the
     *            developers' stage select sets it to the stage set index mod 6 (0x17F44) and
     *            the game's "next mission" step adds 1 (0x1891E; 6 after the final mission, the
     *            ending);
     *   0x10C2F6 the stage (area) being played, 0x10C2F8 / 0x10C2FA the stage set and section
     *            loaded, copied at the load (0x18292) from
     *   0x10C2FC / 0x10C2FE the stage set and section to start at: a new game sets 0 / 0
     *            (0x17F2A); the stage select's entries 1-6 are Missions 1-6 (7-12 Missions 1-6
     *            again in other stages, 13 the ending).
     * Starting at mission N writes the stage set index (0x10C2FD) and the mission (0x10C2F5) after
     * the new game clears them, before the load. */
    .start_mission = 0x10C2FDu,
    .start_mission_also = 0x10C2F5u,
    .start_section_at = 0x10C2FFu,          /* the section word's low byte */
    .missions = 6,
    .start_no_section = 1,
    .mission_now = 0x10C2F5u,
    /* Player 1's score: 4 BCD bytes from 0x10C46A, the HUD's number (19200: 00 01 92 00). */
    .score_p1 = 0x10C46Au,
    /* Objects (docs/BOSS_RUSH.md): a circular list from 0x1010A4 (allocation 0x40BC: +4 previous,
     * +8 next, +0xC parent, +0x10 depth, +0 the routine), life word +0x4E, dead: bit 7 of +0x5F
     * (set when the life goes below 0, e.g. 0xA1214). */
    .object_list = 0x1010A4u,
    .object_life = 0x4E,
    .object_dead = 0x5F,
    /* The game's kill-everything request (0x1067E3 = -$181D(a5), set by 0x4BFE, copied each frame
     * to 0x1067E2 at 0x46D4): every enemy's hit check (0x4BAC, from 0x4C24) then gives itself the
     * death mark (life 0, bit 7 of +0x5F, state 6 at +0x5A), as the debug "D kills all" switch. */
    .kill_flag = 0x1067E3u,
    .boss_min_life = 300,                  /* Mission 4's guns have 500 */
    .bosses = BOSSES,
    .n_bosses = (int)(sizeof BOSSES / sizeof BOSSES[0]),
    .rush_states = RUSH_STATES,
    .n_rush_states = (int)(sizeof RUSH_STATES / sizeof RUSH_STATES[0]),
    /* PLAY MANUAL soft dip (option 5, high byte of 0x10FD8E): read by a new game at 0x16BD4. */
    .manual_word = 0x10FD8Eu,
    .manual_mask = 0xFF00u,

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
