/* What the windowed frontend (window.c) provides to the menus (menu.c) and the sound menu. */
#ifndef FRONTEND_H
#define FRONTEND_H

#include <stdint.h>

#include <SDL3/SDL.h>

#include "config.h"

extern Config cfg;

void fe_notify(const char *fmt, ...);
void fe_save_config(void);
void fe_state_save(void);            /* to slot cfg.slot */
void fe_state_load(void);
void fe_apply_video(void);             /* fullscreen */
void fe_apply_volume(void);
void fe_play(void);                     /* player 1 coin and START, like a player would */
void fe_reset(void);                 /* machine reset (keeps the SRAM) */
void fe_service(void);                  /* test button: BIOS service menu */
void fe_apply_blood(void);
void fe_dev_menu(void);                 /* the game's hidden developer menu (F4) */
void fe_quit(void);
/* Continue where you left off: the game saved on quit (resume.bin). */
int fe_resume_available(void);
int fe_resume(void);
void fe_back_to_start(void);            /* saves the game for "Continue", ends Arcade mode, resets */
/* What the "Continue" panel shows (resume.ini, resume.png); 0 / NULL when there is none.
   mission: 1-based, 0 = unknown; saved: SDL_Time (0 = unknown). */
typedef struct { int mission; uint32_t score; long long saved; } FeResumeInfo;
int fe_resume_info(FeResumeInfo *i);
SDL_Texture *fe_resume_thumbnail(void);
/* Player 1's score and the current mission (1-based), when the game profile has them (else 0). */
uint32_t fe_score_p1(void);
int fe_mission_now(void);
/* Arcade mode (continues: -1 = infinite) and its results. */
void fe_arcade_start(int continues);
void fe_arcade_stop(void);
int fe_arcade_active(void);
void fe_arcade_result(uint32_t *score, uint64_t *frames, int *continues);
/* Boss Rush (bossrush.c): Arcade mode's rules with one credit, from the first boss to the last. */
void fe_rush_start(void);
int fe_rush_mode(void);                  /* the current (or last) Arcade game is a Boss Rush */
void fe_rush_result(int *defeated, int *total, int *cleared);
/* Mission mode: a new game that starts at mission m (0-based). */
void fe_mission_start(int m);
/* The gamepad of player `p` (0 / 1), else the other one, else NULL (the menu's button names). */
SDL_Gamepad *fe_gamepad(int p);
SDL_Gamepad *fe_player_gamepad(int p);     /* player p's own, or NULL */
/* Player p gets the next (d = 1) or previous connected gamepad, remembered (cfg.pad_guid). */
void fe_cycle_gamepad(int p, int d);
/* Window size: s x 224 lines; the largest that fits the monitor. */
void fe_window_size(int s);
int fe_max_window_scale(void);

#endif
