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
    const char *catalog;                /* --catalog-sonido: table of the sound commands */
    int test_sounds;                  /* --probar-sonidos: sound menu test */
    int blood;                          /* --blood: the game's BLOOD option turned on */
    int date[7], has_date;
} Options;

/* headless.c */
int save_bmp(const char *path);
int headless_prepare(const Options *o);
int headless_run(const Options *o);

/* window.c */
int window_run(const Options *o);

/* gog.c: resources\game folder of a GOG installation of Metal Slug X (Windows registry and
 * usual folders). Returns 1 if it found one whose files validate. */
int gog_find(char *folder, int size);

#endif
