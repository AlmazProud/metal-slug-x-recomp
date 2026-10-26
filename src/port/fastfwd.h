/* Fast-forward to a boss (Metal Slug's Boss Rush, docs/BOSS_RUSH.md stage 3).
 *
 * Metal Slug has no stage sections to start a boss fight from: Boss Rush loads a machine state
 * saved shortly before each boss appears. The states are made once, by playing each mission from
 * its start at full speed (no image or sound): the game's own Mission mode start (cheats.h), lives,
 * bombs and the Heavy Machine Gun given again every 600 frames, the enemies with 300 of life or
 * less and the profile's other named bosses given the game's death mark as soon as they appear
 * (killing every enemy whatever its life breaks Mission 2), and a bot (seeded random walk: 40-frame segments going right, left or nowhere, jumping, aiming up,
 * throwing grenades, always firing) with the profile's parameters (ProfileRushState).
 */
#ifndef FASTFWD_H
#define FASTFWD_H

#include <stddef.h>
#include <stdint.h>

#include "profile.h"

/* Every object of the list but the players and the vehicle whose life is 1..max_life and whose
   routine is outside keep_lo..keep_hi (0, 0: none kept) gets the damage routine's death mark
   (life 0 and the dead bit). Needs the profile's object fields. */
void ffwd_kill(int max_life, uint32_t keep_lo, uint32_t keep_hi);
/* Every object whose routine is in lo..hi and has life (1..0x6FFF) gets the death mark. */
void ffwd_kill_range(uint32_t lo, uint32_t hi);
extern int ffwd_kill_max;                 /* the fast-forward's max_life (300; research: PD_FFWD_KILL) */

/* Player 1's values Boss Rush carries from one boss to the next (and a new game's, for the
   first): score (BCD, 4 bytes; with score_p1_digits also its 8 digit bytes), lives, bombs, weapon
   and its ammo. */
typedef struct { uint8_t score[4], digits[8], lives, bombs, weapon, ammo[2]; } FfCarry;
void ffwd_carry_read(FfCarry *c);
void ffwd_carry_write(const FfCarry *c);

enum { FF_RUNNING, FF_DONE, FF_FAILED };

/* Making the states of one mission's bosses (count entries from r, all of r's mission, in order:
   once one has appeared the next one is the target): a power-on, then ffwd_run until FF_DONE (the
   states are ready) or FF_FAILED (a target's max_frames since the last one without it). The
   machine is the emulator's own: whatever ran before is lost. */
void ffwd_begin(const ProfileRushState *r, int count);
int ffwd_run(int frames);
int ffwd_found(void);                      /* targets found so far */
float ffwd_progress(void);                 /* 0..1, by targets and frames against their limits */
/* Target i's state (ng_state_save's) taken about 2 s before it appeared, and a new game's values
   (taken when the player first appears); valid after it was found until the next ffwd_begin. */
const uint8_t *ffwd_state(int i, size_t *size, FfCarry *new_game);
void ffwd_end(void);                       /* frees the states and restores the debug settings */

/* The boss of r present in the object list (alive, life at least boss_min_life). */
int ffwd_boss_present(const ProfileRushState *r);

#endif
