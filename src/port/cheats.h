/* Debug mode and cheats (Phase 7B).
 *
 * Two layers, both built on what the developers left in the game:
 *   - Developer mode: the game believes it runs on SNK's development BIOS (0x10FE80 forced for
 *     the cartridge's reads). In game, SELECT held + D pauses and the same again opens the
 *     developers' hidden menu (debug dips, stage select, test stages, sound test, color bars).
 *     cheats_dev_menu() presses that combination for the player.
 *   - Debug dips without the developer menu: the same switches, forced from the port's own
 *     menu (invincibility, unlimited bombs and ammo, collision boxes...).
 *   - RAM cheats for what the developer menu lacks: give a weapon, bombs, lives, change soldier.
 */
#ifndef CHEATS_H
#define CHEATS_H

#include <stdint.h>

/* Debug dips (word at 0x10F000, read by the game). Bit = the switch in "SET DEBUG DIP". */
enum {
    DIP_INVINCIBLE = 0x8000,
    DIP_INFO = 0x0100,                   /* VIEW INFOMATION: progress, scroll, enemies */
    DIP_PAUSE_ANYTIME = 0x0200,
    DIP_GROUND_A = 0x0800,               /* VIEW GEO SCROLL A / B: ground lines */
    DIP_GROUND_B = 0x1000,
    DIP_ATTACK_BOXES = 0x2000,           /* VIEW ATTACK RECT */
    DIP_BODY_BOXES = 0x4000,             /* VIEW BODY RECT */
    DIP_CPU_METER = 0x0400,              /* not in the menu: darkens the screen while processing */
    DIP_INFINITE_BOMBS = 0x0004,
    DIP_INFINITE_AMMO = 0x0008,
    DIP_PA_POINT = 0x0002,               /* VIEW PA POINT */
    DIP_D_KILLS_ALL = 0x0001,            /* D button: every enemy on screen dies */
};

extern uint16_t cheats_dips;             /* DIP_* forced on (session only) */
extern int cheats_devmode;               /* developer mode (cfg.devmode) */

/* Forced reads for the current settings; call after changing cheats_dips / cheats_devmode. */
void cheats_apply(void);

/* In game only (returns 0 and does nothing outside a mission). player: 0 or 1. */
int cheats_in_game(int player);
int cheats_give_weapon(int player, int weapon);
int cheats_set_bombs(int player, int n);
int cheats_set_lives(int player, int n);
int cheats_set_soldier(int player, int soldier);     /* takes effect on the next life */
int cheats_soldier(int player);

/* Soldier transformations: request one; the game plays its own transformation. */
enum { FORM_NORMAL = 0, FORM_FAT = 2, FORM_MUMMY = 3 };
int cheats_form(int player);
int cheats_set_form(int player, int form);
void cheats_frame(void);
/* Spawning: created like the stage creates its objects, on the next VBlank wait. */
typedef struct { uint16_t type; const char *name; } CheatVehicle;
#define CHEATS_VEHICLE_COUNT 12
extern const CheatVehicle CHEATS_VEHICLES[CHEATS_VEHICLE_COUNT];
int cheats_spawn_vehicle(int vehicle);
int cheats_spawn_type(int type);         /* any stage object type (type*4), for research */             /* queued until the game waits for the VBlank */                 /* after every emulated frame (keeps the fat form) */

#define CHEATS_WEAPONS 16
const char *cheats_weapon_name(int weapon);          /* English; NULL if unused */
extern const char *const CHEATS_SOLDIER_NAME[4];

/* Developer menu: queue SELECT held + D twice (pause, then menu). cheats_input() adds the
 * pending presses to player 1's inputs each frame (system = NG_START/NG_SELECT bits, joy = NG_*). */
void cheats_dev_menu(void);
void cheats_input(uint8_t *joy, uint8_t *system);

#endif
