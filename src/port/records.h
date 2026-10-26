/* Records, saved in records.ini (the preferences folder):
 *   - per mission: the best time and the best score made in it. A mission runs from the start of
 *     the game (or the previous mission's "next mission" step, or the jump of Mission mode) to its
 *     own "next mission" step (game_profile.mission_now goes from n to n + 1, as progress.c);
 *   - Arcade mode: the best score and the furthest mission.
 * Only clean runs count: no debug dips or developer mode, the original slowdown, no state loaded
 * and no cheat used during the mission (records_invalidate).
 */
#ifndef RECORDS_H
#define RECORDS_H

#include <stdint.h>

typedef struct {
    int mission;                         /* 1-based */
    uint32_t frames, score;
    int clean, new_time, new_score;
    int turbo;                           /* the slowdown was off at some point (one reason it is not clean) */
} RecordEvent;

void records_load(const char *path);    /* also sets where it is saved */
int records_supported(void);            /* the profile knows the mission and the score */
/* After every emulated frame in the game: 1 when a mission was just completed (*e filled). */
int records_frame(RecordEvent *e);
void records_invalidate(void);          /* a state was loaded or a cheat used: this mission does not count */
void records_reset_tracking(void);      /* machine reset */
/* Arcade mode's game over: 1 if the score is a new record. */
int records_arcade(uint32_t score, int mission);

/* Boss Rush's end (cleared or game over): 1 if it is a new record (a faster clear, or more bosses
   defeated without a clear yet). */
int records_rush(int defeated, uint32_t frames, int cleared);
uint32_t records_rush_frames(void);      /* best clear time, 0 = never cleared */
int records_rush_defeated(void);         /* most bosses defeated */

/* Best values (0 = none yet). */
uint32_t records_mission_frames(int mission);
uint32_t records_mission_score(int mission);
int records_mission_has(int mission);
uint32_t records_arcade_score(void);
int records_arcade_mission(void);
/* "m:ss.cc" from emulated frames (59.1856 per second). */
void records_time_text(uint32_t frames, char *b, int n);

#endif
