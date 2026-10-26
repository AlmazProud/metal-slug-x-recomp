/* Boss Rush (docs/BOSS_RUSH.md, stage 3): every boss in a row, in one game.
 *
 * Sections (Metal Slug X, 3, 4, 5): the game starts at the first boss's section (the start
 * mission and section bytes, as Mission mode does). When a boss falls the game goes on to its
 * next section by itself; while Boss Rush
 * runs, the stage start routine (game_profile.stage_start_*) reads the next boss's mission and
 * section instead (a forced read, only there): the score, lives, bombs and weapon carry over as
 * between two missions. After the last boss the game's own ending follows.
 * Saved states (Metal Slug, game_profile.rush_states): each boss from a state made once by the
 * fast-forward (fastfwd.h) and kept in a folder of the preferences; see bossrush.c.
 */
#ifndef BOSSRUSH_H
#define BOSSRUSH_H

enum { BR_NONE, BR_NEXT, BR_CLEARED };

int bossrush_supported(void);
int bossrush_total(void);                /* bosses in the run */
/* Sections: before the new game's coin and START. Saved states: loads the first boss (0 if a
   state is missing: bossrush_needs_prepare). Returns 1 if started. */
int bossrush_start(void);
void bossrush_stop(void);
int bossrush_active(void);
/* After every emulated frame: BR_NEXT when a boss section ended (the next one loads),
   BR_CLEARED after the last one. */
int bossrush_frame(void);
int bossrush_defeated(void);             /* bosses defeated so far */
int bossrush_running(void);              /* a boss section is loaded and running */

/* Saved states: the folder (with its trailing separator), whether a state is missing or old, and
   making them, a slice at a time (bossrush_prepare_step: 0 still working, 1 done, -1 failed). */
void bossrush_set_dir(const char *dir);
int bossrush_needs_prepare(void);
void bossrush_prepare_begin(void);
int bossrush_prepare_step(int ms);
void bossrush_prepare_cancel(void);
int bossrush_prepare_boss(void);         /* the boss being prepared (0-based) */
float bossrush_prepare_progress(void);   /* 0..1 */
extern int bossrush_first;               /* tests: saved states, the run starts at this boss (0-based) */
void bossrush_test_defeat(void);         /* tests: the current boss dies (saved states) */

#endif
