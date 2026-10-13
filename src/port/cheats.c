/* Debug mode and cheats (Phase 7B): see cheats.h. Addresses in profile.h. */
#include "cheats.h"

#include "neogeo.h"
#include "profile.h"

uint16_t cheats_dips;
static int keep_fat[2];
int cheats_devmode;

const char *const CHEATS_SOLDIER_NAME[4] = {"Marco", "Eri", "Tarma", "Fio"};

static uint8_t *ram8(uint32_t a) { return &ng.ram[a & 0xFFFF]; }
static void ram16(uint32_t a, uint16_t v) { ram8(a)[0] = (uint8_t)(v >> 8); ram8(a)[1] = (uint8_t)v; }
static uint8_t rom8(uint32_t a) { return ng_roms.p[a]; }
static uint16_t rom16(uint32_t a) { return (uint16_t)(ng_roms.p[a] << 8 | ng_roms.p[a + 1]); }

/* Everything here relies on profile.h: with another version of the game program
   (profile_supported() == 0) nothing is forced and the cheats do nothing. */
void cheats_apply(void)
{
    int ok = profile_supported();
    ng_forced_read[NG_FORCED_DIPS] = (NgForcedRead){ok && cheats_dips != 0, PROFILE_DIPS_WORD, cheats_dips, cheats_dips, 0, 0};
    ng_forced_read[NG_FORCED_DEVMODE] = (NgForcedRead){ok && cheats_devmode, PROFILE_DEVMODE_WORD, 0xFF00, 0x0100, 0, 0};
    ng_forced_read[NG_FORCED_DEVMODE_COPY] = (NgForcedRead){ok && cheats_devmode, PROFILE_DEVMODE_COPY_WORD, 0xFF00, 0x0100, 0, 0};
    ng_forced_read[NG_FORCED_FAT_STAGE] = (NgForcedRead){ok && (keep_fat[0] || keep_fat[1]), PROFILE_STAGE_WORD, 0xFFFF,
                                                         PROFILE_FAT_STAGE, PROFILE_FAT_CHECK_FIRST, PROFILE_FAT_CHECK_LAST};
}

int cheats_in_game(int player)
{
    (void)player;
    return profile_supported() && *ram8(PROFILE_GAME_STATE) == PROFILE_GAME_STATE_PLAYING;
}

/* Same effect as the game's own pickup routine (0xC0928) minus the announcer: weapon, its
 * sub-type, initial ammo and the player object's flags, all from the game's tables in the ROM. */
int cheats_give_weapon(int player, int weapon)
{
    if (!cheats_in_game(player) || weapon < 0 || weapon >= CHEATS_WEAPONS) return 0;
    int p = player & 1;
    uint32_t t = PROFILE_PLAYER_TABLE, obj = p ? PROFILE_PLAYER2_OBJECT : PROFILE_PLAYER1_OBJECT;
    *ram8(t + 6 + p) = (uint8_t)weapon;
    *ram8(t + 4 + p) = rom8(PROFILE_ROM_WEAPON_SUBTYPE + weapon);
    *ram8(t + 2 + p) = 0;
    ram16(t + 0xC + 2 * p, rom16(PROFILE_ROM_WEAPON_AMMO + 2 * weapon));
    *ram8(obj + 0x94) = rom8(PROFILE_ROM_WEAPON_FLAG + weapon);
    *ram8(obj + 0x95) = 0;
    return 1;
}

int cheats_set_bombs(int player, int n)
{
    if (!cheats_in_game(player)) return 0;
    *ram8(PROFILE_PLAYER_TABLE + 8 + (player & 1)) = (uint8_t)(n < 0 ? 0 : n > 99 ? 99 : n);
    return 1;
}

int cheats_set_lives(int player, int n)
{
    if (!cheats_in_game(player)) return 0;
    *ram8((player & 1) ? PROFILE_PLAYER2_LIVES : PROFILE_PLAYER1_LIVES) = (uint8_t)(n < 0 ? 0 : n > 9 ? 9 : n);
    return 1;
}

int cheats_soldier(int player)
{
    return *ram8((player & 1) ? PROFILE_PLAYER2_SOLDIER : PROFILE_PLAYER1_SOLDIER) & 3;
}

int cheats_set_soldier(int player, int soldier)
{
    if (!cheats_in_game(player)) return 0;
    /* The soldier select index (Marco, Eri, Tarma, Fio) and the internal character number
       (Marco 0, Tarma 1, Eri 2, Fio 3) in the status object and in the player object. */
    static const uint8_t INTERNAL[4] = {0, 2, 1, 3};
    int p = player & 1, s = soldier & 3;
    uint32_t status = p ? PROFILE_PLAYER2_SOLDIER : PROFILE_PLAYER1_SOLDIER;
    *ram8(status) = (uint8_t)s;
    *ram8(status - 1) = INTERNAL[s];
    *ram8((p ? PROFILE_PLAYER2_OBJECT : PROFILE_PLAYER1_OBJECT) + PROFILE_OBJECT_CHARACTER) = INTERNAL[s];
    return 1;
}

/* Transformations (player object +0x93 current form, +0x94 requested form). Fat: the food
   routine (0xC0BF4) adds 0x100 per food to the player's counter (table +0x10) and requests form 2
   at 0x780; the game then plays the transformation. Mummy: form 3 requested the same way. */
int cheats_form(int player)
{
    return *ram8(((player & 1) ? PROFILE_PLAYER2_OBJECT : PROFILE_PLAYER1_OBJECT) + PROFILE_OBJECT_FORM);
}

/* Normal form is 0 with the pistol and 1 with any other weapon (pickup table flag). */
static int normal_form(int p)
{
    return rom8(PROFILE_ROM_WEAPON_FLAG + (*ram8(PROFILE_PLAYER_TABLE + 6 + p) & 15));
}

int cheats_set_form(int player, int form)
{
    if (!cheats_in_game(player)) return 0;
    int p = player & 1;
    uint32_t obj = p ? PROFILE_PLAYER2_OBJECT : PROFILE_PLAYER1_OBJECT;
    keep_fat[p] = form == FORM_FAT;
    ram16(PROFILE_PLAYER_TABLE + 0x10 + 2 * p, keep_fat[p] ? PROFILE_FAT_FOOD : 0);
    *ram8(obj + PROFILE_OBJECT_FORM + 1) = (uint8_t)(form == FORM_NORMAL ? normal_form(p) : form);
    cheats_apply();
    return 1;
}

/* ------------------------------------------------------------------ spawning objects */
/* The stages create their objects from records read by the routine at 0x1A0A6 (command 7,
   0x1A10A): [7, length, group, flags, type*4 (word), x, y, parameters...]. Flags bit 7: x is
   relative to the screen. The port builds such a record below the game's stack and calls the
   routine (ng_call68k) while the game waits for the VBlank, so the object is created exactly as
   the stage would. */
/* Vehicles: object types (type*4 in the table at 0x197DC) found by spawning each type of the
   table in Mission 1; the SV-001 record is the one Mission 1 uses (frame 6065). Several are not
   used by Metal Slug X's missions as far as we know. */
const CheatVehicle CHEATS_VEHICLES[CHEATS_VEHICLE_COUNT] = {
    {0x2EC, "Metal Slug (SV-001)"}, {0x2F0, "Metal Slug (gold)"}, {0x2F4, "Camel Slug"},
    {0x2F8, "Slug Flyer"}, {0x2FC, "Slugnoid"}, {0x300, "Ostrich Slug"}, {0x304, "Elephant Slug"},
    {0x308, "Slug Copter"}, {0x30C, "Pod (unknown)"}, {0x310, "Green tank (unknown)"},
    {0x314, "Mounted gun"}, {0x318, "Walker (unknown)"},
};
static int spawn_type = -1;

/* Record [7, length 10, group 9 (none), flags 0x80 (x on screen), type, x, y]. */
int cheats_spawn_type(int type)
{
    if (!cheats_in_game(0)) return 0;
    spawn_type = type;
    return 1;
}

int cheats_spawn_vehicle(int vehicle)
{
    return vehicle >= 0 && vehicle < CHEATS_VEHICLE_COUNT && cheats_spawn_type(CHEATS_VEHICLES[vehicle].type);
}

static void spawn_now(int type)
{
    const uint8_t r[10] = {0x07, 0x0A, 0x09, 0x80, (uint8_t)(type >> 8), (uint8_t)type,
                           PROFILE_SPAWN_X >> 8, PROFILE_SPAWN_X & 0xFF, PROFILE_SPAWN_Y >> 8, PROFILE_SPAWN_Y & 0xFF};
    uint32_t sp = ng_cpu.a[7] & ~1u, rec = sp - 0x400;
    for (int i = 0; i < 10; i++) bus_w8(rec + i, r[i]);
    uint32_t d[8] = {0}, a[8] = {0};
    a[0] = rec;
    a[5] = 0x108000;
    a[6] = PROFILE_SPAWN_PARENT;
    a[7] = sp - 0x100;
    ng_call68k(PROFILE_SPAWN_ROUTINE, d, a, 2000000);
}

/* Every frame: keeps a fat soldier fat (food counter topped up) until he dies or the cheat is
   turned off. */
void cheats_frame(void)
{
    /* Spawn requests wait for a quiet moment: the game waiting for the VBlank. */
    if (spawn_type >= 0 && ng_cpu.pc >= PROFILE_WAIT_START && ng_cpu.pc < PROFILE_WAIT_END) {
        if (cheats_in_game(0)) spawn_now(spawn_type);
        spawn_type = -1;
    }
    for (int p = 0; p < 2; p++) {
        if (!keep_fat[p]) continue;
        uint32_t obj = p ? PROFILE_PLAYER2_OBJECT : PROFILE_PLAYER1_OBJECT;
        int form = *ram8(obj + PROFILE_OBJECT_FORM), pending = *ram8(obj + PROFILE_OBJECT_FORM + 1);
        if (!cheats_in_game(p) || (form != FORM_FAT && pending != FORM_FAT)) { keep_fat[p] = 0; cheats_apply(); continue; }
        ram16(PROFILE_PLAYER_TABLE + 0x10 + 2 * p, PROFILE_FAT_FOOD);
    }
}

const char *cheats_weapon_name(int weapon)
{
    static const char *const NAME[CHEATS_WEAPONS] = {
        /* Identified by their shots and by the announcer voice each one plays (table 0xC08F8);
           7-11 share the voice of 2-6 with bigger shots; 1 has no voice and is not picked up in
           the game as far as we know. */
        "Pistol", "Big blast (unused?)", "Shotgun", "Rocket Launcher", "Flame Shot",
        "Heavy Machine Gun", "Laser Shot", "Big Shotgun", "Big Rocket Launcher", "Big Flame Shot",
        "Big Heavy Machine Gun", "Big Laser Shot", "Enemy Chaser", "Iron Lizard", "Drop Shot",
        "Super Grenade",
    };
    return weapon >= 0 && weapon < CHEATS_WEAPONS ? NAME[weapon] : NULL;
}

/* ------------------------------------------------------------------ developer menu */
/* Frame script relative to the request: SELECT held 0-29, D pressed 6-9 (pause); SELECT held
 * 40-69, D pressed 46-49 (menu). The game reads inputs once per logic tick (2 frames). */
static int dev_frame = -1;

void cheats_dev_menu(void)
{
    if (cheats_devmode) dev_frame = 0;
}

void cheats_input(uint8_t *joy, uint8_t *system)
{
    if (dev_frame < 0) return;
    int f = dev_frame++;
    if ((f < 30) || (f >= 40 && f < 70)) *system |= NG_SELECT;
    if ((f >= 6 && f < 10) || (f >= 46 && f < 50)) *joy |= NG_D;
    if (dev_frame >= 70) dev_frame = -1;
}
