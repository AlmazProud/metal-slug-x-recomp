/* Game profile for the frontend: the per-game data the cheats, the debug options, the red blood
 * option, the title logo and the sound menu rely on (addresses in the game's program and RAM,
 * sound names). Each executable links its game's profile (src/games/<id>_profile.c), which
 * defines `game_profile`. A game without a profile (n_windows == 0) has those options off.
 */
#ifndef PROFILE_H
#define PROFILE_H

#include <stdint.h>

typedef struct { uint32_t first, last; } ProfileWindow;
typedef struct { int type; const char *name; } CheatVehicle;
typedef struct { int form; const char *name; } CheatForm;
/* Boss Rush: a boss section (the start mission and section bytes) and the bosses in it. */
typedef struct { uint8_t mission, section, bosses; } ProfileRush;
/* Boss Rush from saved states (Metal Slug: no sections; fastfwd.c, bossrush.c): a boss (an index
   into the profile's bosses), its mission (0-based), whether the mission ends with it, and the
   fast-forward bot that reaches it from the mission's start (fastfwd.h: seed, percentages of
   segments going right / left, jumping and throwing a grenade; frames before giving up). */
typedef struct {
    uint8_t boss, mission, final;
    uint8_t right, left, jump, grenade;
    uint32_t seed, max_frames;
} ProfileRushState;
/* A boss's code (its object's routine, +0, is between code_lo and code_hi), its full life, its
   name (shown on the health bar; NULL: "BOSS") and the mission it is a boss in (1-based, read at
   mission_now; 0: any. Metal Slug: Shoe & Karn's tank is also an unnamed one in Mission 5). */
typedef struct { uint32_t code_lo, code_hi; int life; const char *name; int mission; } ProfileBoss;
enum { DEV_MENU_SELECT_D = 1, DEV_MENU_D_SELECT = 2 };

/* Sound driver commands: driver mode (00 music, 10 effects, 11 voices), command, duration in
 * tenths of a second (0xFFFF = looping) and name. */
typedef struct {
    uint8_t mode, command;
    uint16_t tenths;
    const char *name;
} Sound;

/* A sound driver mode (the command that selects it) and the sound menu tab its sounds go to. */
enum { SOUND_MUSIC, SOUND_EFFECTS, SOUND_VOICES };
typedef struct { uint8_t mode, category; } SoundMode;

/* Gamepad rumble of an effect (driver mode 10): big motor, small motor, milliseconds. */
typedef struct { uint8_t command; uint16_t low, high, ms; } SoundRumble;

/* Debug dips (bits of the word at dips_word) behind the frontend's switches; 0 = the game has
   none. The names are the developers' own "SET DEBUG DIP" menu, in the game's program. */
typedef struct {
    uint16_t invincible, infinite_bombs, infinite_ammo, d_kills_all;
    uint16_t boxes, info, ground, cpu_meter, pa_point, pause_anytime;
} ProfileDips;

typedef struct {
    const char *title;                  /* start menu title, uppercase */
    const char *subtitle;
    /* Program windows (68000 view) that must match `windows_sha256` for the addresses below to
       be used: one SHA-256 over all of them, in order. */
    const ProfileWindow *windows;
    int n_windows;
    const char *windows_sha256;
    const CheatVehicle *vehicles;       /* object types that the vehicle cheat spawns */
    int n_vehicles;
    /* Vehicles as a fixed object (Metal Slug; 0: stage object records, spawn_routine): the
       object, the game's table of its first states in the ROM (a long per vehicle type, the
       CheatVehicle's type), the values of a free object's first long, and where the object keeps
       its position (x, y and a copy of y, words) and a byte the game's creation clears; it appears
       at player 1's position, spawn_x pixels ahead. */
    uint32_t vehicle_object, vehicle_states;
    uint32_t vehicle_free[4];
    uint8_t vehicle_x_at, vehicle_y_at, vehicle_y2_at, vehicle_clear_at;
    const Sound *sounds;                /* built-in sound names */
    const int *n_sounds;
    const SoundRumble *rumble;          /* rumble of the identified effects */
    int n_rumble;
    /* The sound driver's modes (up to 4) and the commands that silence it (the first one also
       ends the current mode). 0: Metal Slug X and 3's driver, modes 00 music, 10 effects, 11
       voices, silenced by 03 03 06. */
    const SoundMode *sound_modes;
    int n_sound_modes;
    const uint8_t *sound_stop;
    int n_sound_stop;
    /* Addresses and values (see the game's profile file for what each one is). */
    uint32_t wait_start;
    uint32_t wait_end;
    uint32_t ram_frames_per_tick;
    uint32_t dips_word;
    uint32_t devmode_word;
    uint32_t devmode_copy_word;
    uint32_t game_state;
    uint32_t game_state_playing;
    uint32_t player_table;
    uint32_t player1_object;
    uint32_t player2_object;
    uint32_t object_character;
    uint32_t object_form;
    uint32_t fat_food;
    uint32_t stage_word;
    uint32_t fat_check_first;
    uint32_t fat_check_last;
    uint32_t fat_stage;
    uint32_t spawn_routine;
    uint32_t spawn_parent;
    uint32_t spawn_x;
    uint32_t spawn_y;
    uint32_t player1_soldier;
    uint32_t player2_soldier;
    uint32_t player1_lives;
    uint32_t player2_lives;
    uint32_t rom_weapon_subtype;
    uint32_t rom_weapon_ammo;
    uint32_t rom_weapon_flag;
    uint32_t blood_word;
    uint32_t blood_mask;
    /* The PLAY MANUAL soft dip (the "How to play" after START): Mission mode's direct start reads
       it as off (cheats.h, cheats_mission_from). 0 = not known: Mission mode waits for the
       attract mode and starts the mission when a player is playing. */
    uint32_t manual_word;
    uint32_t manual_mask;
    uint32_t logo_first;
    uint32_t logo_last;
    uint32_t logo_tm;
    uint32_t logo_tm_tile;
    uint32_t logo_max_frames;
    /* Fast boot: frames from power-on to the game's first image (the BIOS boot before it). */
    uint32_t boot_frames;
    /* The window covers the image's two bottom corners in black, this many pixels square (0: no).
       Metal Slug blacks its 8-pixel side columns with fix tiles on every row but the last (the
       CREDITS line), where the background shows; the emulated image (MAME's) is not changed. */
    uint8_t black_bottom_corners;
    /* Start mission: the game keeps the mission and the section to start at in two bytes
       (start_mission, start_mission + 1); a new game sets both to 0 and loads the mission a moment
       later. missions = 0: not supported. */
    uint32_t start_mission;
    uint32_t missions;
    /* 1: the byte after start_mission is not a section (Metal Slug: the memory card continue's
       player status), so a mission counts as loaded when mission_now equals it with a player in
       the game; until then a one-time start is written again whenever the game clears it. */
    int start_no_section;
    /* Another byte the start mission is written to with start_mission (0: none). Metal Slug X:
       its start byte is the stage set index (0x10C2FD, entries 1-6 of the developers' stage
       select are Missions 1-6) and the mission number (0x10C2F5, mission_now) is only set from it
       by the stage select, so both are written. */
    uint32_t start_mission_also;
    /* Mission and Arcade modes (progress.c, window.c): the current mission byte (0-based; the
       game's "next mission" step makes it n + 1) and player 1's score (4 bytes, BCD). */
    uint32_t mission_now;
    uint32_t score_p1;
    /* Where the game keeps player 1's score when score_p1 is only its BCD copy (0: score_p1 is
       the score). Metal Slug: 8 bytes of one digit each, packed into score_p1 to add points. */
    uint32_t score_p1_digits;
    ProfileDips dips;
    /* Player table layout: a field is at player_table + offset + player * stride. Metal Slug X
       interleaves both players (stride 1 for bytes, 2 for words); Metal Slug 3 has a block per
       player. */
    uint32_t table_stride_byte, table_stride_word;
    uint32_t table_shots, table_subtype, table_weapon, table_bombs, table_ammo;
    /* Weapons: count, names (English) and the ROM tables the game's pickup routine reads, with
       their strides (rom_weapon_flag = 0: the pickup clears the player object's flag). */
    uint32_t weapons;
    const char *const *weapon_names;
    uint32_t rom_weapon_subtype_stride, rom_weapon_ammo_stride;
    /* The player object's byte the pickup clears (0: 0x94, Metal Slug X and 3) and the player
       table's previous weapon, which the pickup sets to the weapon it replaces (0: none). */
    uint32_t object_pickup_flag;
    uint32_t table_previous_weapon;
    /* The game's own "give a weapon" routine (D0 player, D1 weapon), the one its weapon items
       call (0: none). The weapon cheat calls it while the game waits for the VBlank. */
    uint32_t give_routine;
    /* Soldier: 1 if player*_soldier holds only the internal character (Marco 0, Tarma 1, Eri 2,
       Fio 3), used from the next life; 0 (Metal Slug X): the soldier select index, with the
       internal character in the byte before it and in the player object. */
    int soldier_internal;
    /* The soldiers in the soldier select's order and the internal character of each (NULL: Marco,
       Eri, Tarma, Fio and 0, 2, 1, 3; Metal Slug 4: Marco, Fio, Trevor, Nadia and 0, 3, 1, 2). */
    const char *const *soldier_names;
    const uint8_t *soldier_internal_of;
    /* In game without game_state: BIOS player status (0x10FDB6 + player) = 1 and the player
       object's pointer at object_table_ptr pointing to its table. */
    uint32_t object_table_ptr;
    /* The developers' menu (cheats_dev_menu), with the developer mode on: a combination pressed
       twice, the first one pauses. DEV_MENU_SELECT_D: SELECT held, D pressed (Metal Slug X);
       DEV_MENU_D_SELECT: D held, SELECT pressed (Metal Slug 3); 0: not verified. */
    int dev_menu;
    /* Transformations the form cheat offers (forms[0]: normal, whose form depends on the weapon:
       rom_weapon_form, a byte per weapon), the food counter (player table word) that keeps the
       fat form, and the help line (English). */
    const CheatForm *forms;
    int n_forms;
    uint32_t rom_weapon_form;
    /* The game's form table (0: Metal Slug X and 3, the forms are requested as they are): the
       requested form is form_table[(event * 3 + weapon class) * 4 + form class], with the weapon
       class from form_weapon_class (a byte per weapon) and the current form's class from
       form_class (a byte per form). Event 1 makes the soldier fat (form class fat_class), event 2
       brings him back to normal (Metal Slug 5: 0x1C238, 0x1C1C4, 0x1C1D8). form_weapon_classes and
       form_classes replace the 3 and the 4 (0: Metal Slug 5's; Metal Slug 4: 2 and 10). */
    uint32_t form_table, form_weapon_class, form_class;
    uint8_t form_weapon_classes, form_classes;
    /* With the form table: form_events[i - 1] for each forms[i] (i >= 1), the event that requests
       it and the event that brings it back to normal (NULL: fat only, events 1 and 2). */
    const uint8_t (*form_events)[2];
    uint8_t fat_class;
    uint32_t table_food;
    const char *form_help;
    /* 1: from a form other than normal and fat the cheat goes back to normal first (the only
       change the game itself makes from them). */
    int form_normal_first;
    /* Spawn record (cheats.c): its first 4 bytes (command, length 10, group, flags) and where the
       type, x and y words go. */
    uint8_t spawn_head[4];
    uint8_t spawn_type_at, spawn_x_at, spawn_y_at;
    /* 0: spawn_x / spawn_y are the record's x and y. Otherwise the address of the scroll layer's
       screen position (two longs, 16.16) the record's x and y are relative to: the object then
       appears at spawn_x / spawn_y from player 1 (its object's screen position, longs at +0x3C
       and +0x40). */
    uint32_t spawn_layer;
    /* The object's screen position for spawn_layer: x long at this offset, y long after it (0: 0x3C,
       Metal Slug 3; Metal Slug 5: 0x42). */
    uint32_t object_position;
    /* Weapon items: the stage object type of the items (0: the weapon cheat gives the weapon
       directly), its x offset from player 1 and the item kind of each weapon (0: none, the weapon
       is given directly). */
    uint32_t item_type, item_x;
    const uint8_t *weapon_items;
    /* Boss health bar (bossbar.c): the object list's head (links at +4, parent at +0xC), each
       object's life word and dead byte (bit 7) offsets, and the code of the known bosses (an
       object whose routine is there is labelled BOSS). object_list = 0: no bar. */
    uint32_t object_list;
    uint16_t object_life, object_dead;
    uint8_t object_dead_mask;           /* the dead bit in that byte (0: bit 7; Metal Slug: bit 0) */
    /* The game's damage routine (A1 the object, D0 the damage, A6 the attacker: its +0x68 is the
       player who scores, negative for none); Boss Rush's tests defeat a boss with it. 0: none. */
    uint32_t damage_routine;
    /* The HUD's routine that draws player 1's lives (A6: hud_object_p1, the object holding
       player1_lives); Boss Rush calls it after giving a boss's state the lives carried (the HUD
       only redraws them when the player dies). 0: none. */
    uint32_t hud_lives_routine, hud_object_p1;
    /* The smallest life a boss is followed from (0: 1000; Metal Slug 4: bosses and phases of 500)
       and 1 if a life of 0x8000 or more is below 0 (Metal Slug 4's defeated bosses), not
       indestructible. */
    uint16_t boss_min_life;
    uint8_t boss_life_signed;
    const ProfileBoss *bosses;
    int n_bosses;
    /* Boss Rush (bossrush.c): the boss sections in order, and the stage start routine that reads
       the start bytes (start_mission, start_mission + 1). n_rush = 0: no Boss Rush. */
    const ProfileRush *rush;
    int n_rush;
    int rush_weapon;          /* weapon whose item appears in front of the player at the start (0: none) */
    /* Boss Rush from saved states instead of sections (n_rush = 0): the bosses in order. */
    const ProfileRushState *rush_states;
    int n_rush_states;
    uint32_t stage_start_first, stage_start_last;
} GameProfile;

extern const GameProfile game_profile;

/* profile.c: 1 if the loaded program has the reference copy's code in every profile window
 * (the cheats, debug options and red blood are only offered then). */
int profile_supported(void);

#endif
