/* Run-ahead (docs/QOL_FEATURES.md): the game's reaction to an input is shown sooner by emulating
 * the next frames ahead and showing the last one's image, then going back.
 *
 * After a real frame, runahead_frames(n) saves the machine's state, runs n frames with the same
 * inputs (no audio, and none of the frontend's hooks: rumble, Boss Rush, fingerprints, logo),
 * keeps the last image, loads the state back and puts that image in ng.screen. The real frames
 * are the same as without it: the game, its audio and every comparison with MAME do not change
 * (test mslug3_start_runahead_vs_mame). Metal Slug 3 answers 1 or 2 frames after an input
 * (analysis\runahead\measure_lag.py): 1 frame ahead always shows it a frame sooner.
 */
#ifndef RUNAHEAD_H
#define RUNAHEAD_H

#define RUNAHEAD_MAX 2

/* Returns 1 if the image ahead is in ng.screen. */
int runahead_frames(int n);

#endif
