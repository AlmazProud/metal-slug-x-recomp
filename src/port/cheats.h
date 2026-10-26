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
#include <stdio.h>

#include "profile.h"

extern uint16_t cheats_dips;             /* debug dips forced on (game_profile.dips; session only) */
extern int cheats_devmode;               /* developer mode (cfg.devmode) */
extern int cheats_start_mission;         /* 0 = normal; n = new games start at mission n + 1 */
/* Mission mode: the next new game starts at this mission (0-based; -1 = none), then it goes back
   to -1. Takes precedence over cheats_start_mission. */
extern int cheats_start_mission_once;
/* With it: the section (record of the mission's stage group) to start at (-1 = none). */
extern int cheats_start_section_once;
/* Mission mode's direct start (window.c fe_mission_start, after a reset; needs
   game_profile.manual_word): from this frame (ng.frames; 0 = off) the game's PLAY MANUAL reads as
   off and the once mission is written whenever the start bytes are 0, player playing or not (the
   soldier select comes first), until the game has loaded it (section byte 0xFF); then it goes
   back to 0. A first write can be cleared again by the game (a new game, the "How to play"), so
   one write is not enough. */
extern uint64_t cheats_mission_from;
/* Research and tests (headless PD_SPAWN_POKES): each spawn writes the RAM bytes it changed there,
   as PD_POKE entries, so that a MAME run can make the same change at the same frame. */
extern FILE *cheats_spawn_log;

/* Forced reads for the current settings; call after changing cheats_dips / cheats_devmode. */
void cheats_apply(void);

/* In game only (returns 0 and does nothing outside a mission). player: 0 or 1. */
int cheats_in_game(int player);
int cheats_give_weapon(int player, int weapon);
int cheats_give_pending(void);            /* a weapon given through the game's routine not yet done */
int cheats_set_bombs(int player, int n);
int cheats_set_lives(int player, int n);
int cheats_set_soldier(int player, int soldier);     /* takes effect on the next life */
int cheats_soldier(int player);

/* Soldier transformations (the game's form numbers, game_profile.forms): request one; the game
   plays its own transformation. FORM_NORMAL picks the normal form for the current weapon. Fat is
   2 in both games. */
enum { FORM_NORMAL = 0, FORM_FAT = 2 };
int cheats_form(int player);
int cheats_set_form(int player, int form);
void cheats_frame(void);
/* Spawning: created like the stage creates its objects, on the next VBlank wait. */
/* The vehicles are the game profile's (game_profile.vehicles). */
int cheats_spawn_vehicle(int vehicle);
int cheats_spawn_type(int type);         /* any stage object type (type*4), for research */             /* queued until the game waits for the VBlank */                 /* after every emulated frame (keeps the fat form) */

int cheats_weapons(void);                            /* game_profile.weapons */
const char *cheats_weapon_name(int weapon);          /* English; NULL if unused */
extern const char *const CHEATS_SOLDIER_NAME[4];
const char *cheats_soldier_name(int soldier);      /* the profile's names, else CHEATS_SOLDIER_NAME */

/* Developer menu: queue SELECT held + D twice (pause, then menu). cheats_input() adds the
 * pending presses to player 1's inputs each frame (system = NG_START/NG_SELECT bits, joy = NG_*). */
void cheats_dev_menu(void);
void cheats_input(uint8_t *joy, uint8_t *system);

#endif
