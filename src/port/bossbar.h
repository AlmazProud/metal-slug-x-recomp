/* Boss health bar (docs/BOSS_RUSH.md, stage 2): drawn by the frontend over the game image from
 * the objects' life in RAM; the game is not changed.
 *
 * Every object keeps its life in a word (game_profile.object_life) and a dead bit
 * (game_profile.object_dead, bit 7). Only the bosses are followed: objects of the list whose
 * routine is in a known boss's code (game_profile.bosses) with a life between 1000 and 0x6FFF
 * (larger values are indestructible scenery); the bar appears at the first hit, with the boss's
 * name, and stays while it lives.
 */
#ifndef BOSSBAR_H
#define BOSSBAR_H

#include <SDL3/SDL.h>

int bossbar_supported(void);
/* After every emulated frame: follows the bosses' life. */
void bossbar_frame(void);
void bossbar_reset(void);              /* machine reset, state loaded */
/* Over the game image (its rectangle in output pixels). */
void bossbar_draw(SDL_Renderer *r, SDL_FRect game);
/* Tests: every followed boss counts as hit (window checks without inputs). */
void bossbar_test_engage(void);
/* Tests: the bar's state as text ("" when hidden). */
const char *bossbar_describe(void);

#endif
