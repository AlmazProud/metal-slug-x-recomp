/* Mission mode progress: which missions are unlocked, saved in progress.ini (the preferences
 * folder). A mission is unlocked when the previous one is completed in any mode: the game's
 * current mission (game_profile.mission_now) goes from n to n + 1 while a player is playing.
 */
#ifndef PROGRESS_H
#define PROGRESS_H

void progress_load(const char *path);   /* also sets where it is saved */
int progress_unlocked(void);            /* missions available, 1..game_profile.missions */
int progress_supported(void);           /* the game profile knows the current mission */
/* After every emulated frame: the mission just unlocked (1-based) or 0. */
int progress_frame(void);
/* 1 once after the last mission's step (the final boss fell: the ending follows). */
int progress_take_completed(void);
/* Boss Rush is unlocked by completing the game in Arcade or Mission mode (window.c decides). */
int progress_rush_unlocked(void);
void progress_unlock_rush(void);

#endif
