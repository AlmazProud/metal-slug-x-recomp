/* Native port: pieces shared by main.c, windowed mode and headless mode. */
#ifndef PORT_H
#define PORT_H

#define NS_PER_FRAME 16896000LL         /* 202 752 cycles at 12 MHz: 59.1856 Hz */

typedef struct {
    const char *roms;
    int scale, headless, empty_sram;
    int turbo;                           /* -1 = the one from the configuration */
    long frames;
    const char *screenshots, *output, *script, *wav, *inputs, *fingerprints, *dump;
    int no_audio;
    int test_state;                   /* frames to compare after loading a state (0 = no) */
    const char *catalog;                /* --sound-catalog: table of the sound commands */
    int test_sounds;                  /* --test-sounds: sound menu test */
    int blood;                          /* --blood: the game's BLOOD option turned on */
    int date[7], has_date;
} Options;

/* main.c: 1 if the recompiled program runs the loaded copy (same P1 and BIOS it was generated
 * from); otherwise the 68000 runs in the interpreter. */
int machine_recompiled(void);

/* headless.c */
int save_bmp(const char *path);
int headless_prepare(const Options *o);
int headless_run(const Options *o);

/* window.c */
int window_run(const Options *o);

/* find_game.c: places where the user's copy may be, in order of preference ("game" and "roms"
 * next to the executable, GOG, Steam, usual folders). Returns how many were written to `list`. */
int game_places(const char *exe_dir, char (*list)[1024], int max);

#endif
